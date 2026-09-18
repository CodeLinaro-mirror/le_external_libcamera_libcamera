/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * Copyright 2025 Renesas Electronics Co
 * Copyright 2025 Niklas Söderlund <niklas.soderlund@ragnatech.se>
 *
 * Renesas R-Car Gen4 ISP pipeline
 */

#include <memory>
#include <queue>
#include <string>
#include <vector>

#include <libcamera/base/utils.h>

#include <libcamera/formats.h>
#include <libcamera/stream.h>

#include <libcamera/ipa/core_ipa_interface.h>
#include <libcamera/ipa/rppx1_ipa_interface.h>
#include <libcamera/ipa/rppx1_ipa_proxy.h>

#include "libcamera/internal/camera.h"
#include "libcamera/internal/camera_sensor.h"
#include "libcamera/internal/delayed_controls.h"
#include "libcamera/internal/device_enumerator.h"
#include "libcamera/internal/framebuffer.h"
#include "libcamera/internal/ipa_manager.h"
#include "libcamera/internal/media_device.h"
#include "libcamera/internal/pipeline_handler.h"
#include "libcamera/internal/request.h"
#include "libcamera/internal/v4l2_subdevice.h"
#include "libcamera/internal/v4l2_videodevice.h"

#include "frames.h"
#include "isp.h"
#include "vin.h"

namespace libcamera {

namespace {

static constexpr unsigned int kMaxRequests = 4;
static constexpr unsigned int kDefaultBufferCount = kMaxRequests;

} /* namespace */

LOG_DEFINE_CATEGORY(RCar4)

/* -----------------------------------------------------------------------------
 * Camera Data
 */

class RCar4CameraData final : public Camera::Private
{
public:
	RCar4CameraData(PipelineHandler *pipe)
		: Camera::Private(pipe)
	{
	}

	int init(const MediaDevice *mdev, const std::string &pipeId);

	[[nodiscard]]
	bool populateFormats();

	void updateControls();

	[[nodiscard]]
	std::tuple<PixelFormat, unsigned int, Size>
	findSensorFormat(PixelFormat pixelFormat, Size size, Transform transform) const;

	/* Slots for processing ready buffers. */
	void vinBufferReady(FrameBuffer *buffer);
	void inputBufferReady(FrameBuffer *buffer);
	void paramBufferReady(FrameBuffer *buffer);
	void statBufferReady(FrameBuffer *buffer);
	void outputBufferReady(FrameBuffer *buffer);

	/* Slots for processing IPA interactions. */
	void paramsComputed(unsigned int frame, unsigned int bytesused);
	void setSensorControls(unsigned int frame,
			       const ControlList &sensorControls);
	void metadataReady(unsigned int frame, const ControlList &metadata);

	RCarVINDevice vin_;
	RCarISPDevice isp_;
	std::unique_ptr<ipa::rppx1::IPAProxyRppX1> ipa_;

	RCar4Frames frames_;
	std::unique_ptr<DelayedControls> delayedCtrls_;
	ControlInfoMap ipaControls_;

	std::map<unsigned int, std::vector<Size>> rawFormats_;
	std::map<PixelFormat, std::vector<Size>> outputFormats_;
};

int RCar4CameraData::init(const MediaDevice *mdev, const std::string &pipeId)
{
	int ret;

	ret = vin_.init(mdev, pipeId);
	if (ret)
		return ret;

	ret = isp_.init(mdev, pipeId);
	if (ret)
		return ret;

	/*
	 * Load the RPP-X1 IPA for use with RCar4.
	 */
	ipa_ = pipe()->createIPA<ipa::rppx1::IPAProxyRppX1>("rppx1", 1, 1);
	if (!ipa_) {
		LOG(RCar4, Error) << "No IPA module found";
		return -ENOENT;
	}

	/* The IPA tuning file is made from the sensor name. */
	std::string ipaTuningFile = ipa_->configurationFile(
		vin_.sensor()->model() + ".yaml", "uncalibrated.yaml");

	IPACameraSensorInfo sensorInfo;
	ret = vin_.sensor()->sensorInfo(&sensorInfo);
	if (ret) {
		LOG(RCar4, Error) << "Camera sensor information not available";
		return ret;
	}

	IPASettings settings{
		std::move(ipaTuningFile),
		vin_.sensor()->model(),
	};

	ret = ipa_->init(std::move(settings), sensorInfo,
			 vin_.sensor()->controls(), &ipaControls_);
	if (ret < 0) {
		LOG(RCar4, Error) << "IPA initialization failure";
		return ret;
	}

	updateControls();

	/*
	 * Initialize the camera properties.
	 */
	properties_ = vin_.sensor()->properties();
	const CameraSensorProperties::SensorDelays &delays = vin_.sensor()->sensorDelays();
	std::unordered_map<uint32_t, DelayedControls::ControlParams> params = {
		{ V4L2_CID_ANALOGUE_GAIN, { delays.gainDelay, false } },
		{ V4L2_CID_EXPOSURE, { delays.exposureDelay, false } },
		{ V4L2_CID_VBLANK, { delays.vblankDelay, true } },
	};

	delayedCtrls_ = std::make_unique<DelayedControls>(
		vin_.sensor()->device(), params);

	/* Connect bufferReady for each video device to a handler. */
	vin_.bufferReady().connect(this, &RCar4CameraData::vinBufferReady);
	isp_.input_->bufferReady.connect(this, &RCar4CameraData::inputBufferReady);
	isp_.param_->bufferReady.connect(this, &RCar4CameraData::paramBufferReady);
	isp_.stat_->bufferReady.connect(this, &RCar4CameraData::statBufferReady);
	isp_.output_->bufferReady.connect(this, &RCar4CameraData::outputBufferReady);

	/* Connect IPA signals. */
	ipa_->setSensorControls.connect(this, &RCar4CameraData::setSensorControls);
	ipa_->paramsComputed.connect(this, &RCar4CameraData::paramsComputed);
	ipa_->metadataReady.connect(this, &RCar4CameraData::metadataReady);

	/* Apply controls at start of frame. */
	vin_.frameStart().connect(delayedCtrls_.get(), &DelayedControls::applyControls);

	if (!populateFormats()) {
		LOG(RCar4, Error)
			<< "Sensor " << vin_.sensor()->entity()->name()
			<< " has no format and size compatible with the VIN and ISP";
		return -ENOTSUP;
	}

	return 0;
}

namespace {

/*
 * \todo This should obviously be common code.
 */
void filterSizes(std::vector<Size> &sizes, std::span<const SizeRange> filter)
{
	for (auto it = sizes.begin(); it != sizes.end();) {
		bool accept = false;

		for (const auto &range : filter) {
			accept = range.contains(*it);
			if (accept)
				break;
		}

		if (!accept)
			it = sizes.erase(it);
		else
			++it;
	}
}

} /* namespace */

/*
 * \todo This should obviously be common code.
 */
bool RCar4CameraData::populateFormats()
{
	const auto &vinFormats = vin_.output()->formats();
	const auto &inputFormats = isp_.input_->formats();
	std::set<Size> outputSizes;

	rawFormats_.clear();
	outputFormats_.clear();

	for (unsigned int mbusCode : vin_.sensor()->mbusCodes()) {
		auto v4pf = BayerFormat::fromMbusCode(mbusCode).toV4L2PixelFormat();

		auto it = vinFormats.find(v4pf);
		if (it == vinFormats.end())
			continue;

		auto it2 = inputFormats.find(v4pf);
		if (it2 == inputFormats.end())
			continue;

		auto sizes = vin_.sensor()->sizes(mbusCode);
		filterSizes(sizes, it->second);
		filterSizes(sizes, it2->second);

		if (sizes.empty())
			continue;

		/*
		 * \todo This assumes any input size is accepted as output size
		 * for all output formats.
		 */
		outputSizes.insert(sizes.begin(), sizes.end());

		rawFormats_.try_emplace(mbusCode, std::move(sizes));
	}

	for (const auto &[v4pf, sizes] : isp_.output_->formats()) {
		auto pf = v4pf.toPixelFormat();
		if (!pf.isValid())
			continue;

		outputFormats_.try_emplace(pf, outputSizes.begin(), outputSizes.end());
	}

	return !rawFormats_.empty() && !outputFormats_.empty();
}

void RCar4CameraData::updateControls()
{
	ControlInfoMap::Map controls{
		ipaControls_.begin(), ipaControls_.end()
	};

	controlInfo_ = { std::move(controls), controls::controls };
}

/*
 * \todo This should obviously be common code.
 *
 * CameraSensor::getFormat() is not adequate as it cannot take
 * specific requirements along a pipeline into account.
 */
std::tuple<PixelFormat, unsigned int, Size>
RCar4CameraData::findSensorFormat(PixelFormat targetFormat, Size targetSize,
				  Transform transform) const
{
	struct {
		unsigned int mbusCode;
		PixelFormat pf;
		Size size;
		unsigned bpp;
		uint64_t areaDiff = -1;
	} best = {};

	const auto targetArea = uint64_t(targetSize.width) * targetSize.height;

	for (const auto &[mbusCode, sizes] : rawFormats_) {
		ASSERT(!sizes.empty());

		auto bayerFormat = BayerFormat::fromMbusCode(mbusCode);
		ASSERT(bayerFormat.isValid());
		bayerFormat.order = vin_.sensor()->bayerOrder(transform);

		auto pf = bayerFormat.toPixelFormat();
		ASSERT(pf.isValid());

		const auto &info = PixelFormatInfo::info(pf);

		for (const Size &size : sizes) {
			const auto area = uint64_t(size.width) * size.height;
			const auto areaDiff = utils::abs_diff(targetArea, area);

			if ((pf == targetFormat && best.pf != targetFormat) ||
			    areaDiff < best.areaDiff ||
			    (areaDiff == best.areaDiff && info.bitsPerPixel > best.bpp))
				best = { mbusCode, pf, size, info.bitsPerPixel, areaDiff };
		}

		if (targetFormat.isValid() && best.pf == targetFormat)
			break;
	}

	LOG(RCar4, Debug)
		<< "format: " << best.pf << ", "
		<< "size: " << best.size;

	/*
	 * The un-transformed mbus code is returned as it is expected
	 * that the sensor driver handles that correctly.
	 */

	return { best.pf, best.mbusCode, best.size };
}

void RCar4CameraData::vinBufferReady(FrameBuffer *buffer)
{
	RCar4Frames::Info *info = frames_.find(buffer);
	if (!info)
		return;

	Request *request = info->request;

	/* If the buffer is cancelled force a complete of the whole request. */
	if (buffer->metadata().status == FrameMetadata::FrameCancelled) {
		frames_.remove(info);
		request->_d()->cancel();
		pipe()->completeRequest(request);
		return;
	}

	/* Record the sensor's timestamp in the request metadata. */
	request->_d()->metadata().set(controls::SensorTimestamp,
				      buffer->metadata().timestamp);

	ipa_->computeParams(info->frame, info->paramBuffer->cookie());
}

void RCar4CameraData::inputBufferReady(FrameBuffer *buffer)
{
	RCar4Frames::Info *info = frames_.find(buffer);
	if (!info)
		return;

	Request *request = info->request;

	if (request->findBuffer(&frames_.rawStream_))
		pipe()->completeBuffer(request, buffer);

	info->rawDequeued = true;

	if (frames_.tryComplete(info))
		pipe()->completeRequest(request);
}

void RCar4CameraData::paramBufferReady(FrameBuffer *buffer)
{
	RCar4Frames::Info *info = frames_.find(buffer);
	if (!info)
		return;

	Request *request = info->request;

	info->paramDequeued = true;

	if (frames_.tryComplete(info))
		pipe()->completeRequest(request);
}

void RCar4CameraData::statBufferReady(FrameBuffer *buffer)
{
	RCar4Frames::Info *info = frames_.find(buffer);
	if (!info)
		return;

	Request *request = info->request;

	if (buffer->metadata().status == FrameMetadata::FrameCancelled) {
		info->metadataProcessed = true;

		if (frames_.tryComplete(info))
			pipe()->completeRequest(request);

		return;
	}

	ipa_->processStats(info->frame, info->statBuffer->cookie(),
			   delayedCtrls_->get(buffer->metadata().sequence));
}

void RCar4CameraData::outputBufferReady(FrameBuffer *buffer)
{
	RCar4Frames::Info *info = frames_.find(buffer);
	if (!info)
		return;

	Request *request = info->request;

	if (request->findBuffer(&frames_.outputStream_))
		pipe()->completeBuffer(request, buffer);

	request->_d()->metadata().set(controls::draft::PipelineDepth, 3);

	info->outputDequeued = true;

	if (frames_.tryComplete(info))
		pipe()->completeRequest(request);
}

void RCar4CameraData::paramsComputed(unsigned int frame, unsigned int bytesused)
{
	RCar4Frames::Info *info = frames_.find(frame);
	if (!info)
		return;

	info->paramBuffer->_d()->metadata().planes()[0].bytesused = bytesused;

	isp_.output_->queueBuffer(info->outputBuffer);
	isp_.param_->queueBuffer(info->paramBuffer);
	isp_.stat_->queueBuffer(info->statBuffer);
	isp_.input_->queueBuffer(info->inputBuffer);
}

void RCar4CameraData::setSensorControls([[maybe_unused]] unsigned int frame,
					const ControlList &sensorControls)
{
	delayedCtrls_->push(sensorControls);
}

void RCar4CameraData::metadataReady(unsigned int frame, const ControlList &metadata)
{
	RCar4Frames::Info *info = frames_.find(frame);
	if (!info)
		return;

	Request *request = info->request;

	info->request->_d()->metadata().merge(metadata);
	info->metadataProcessed = true;

	if (frames_.tryComplete(info))
		pipe()->completeRequest(request);
}

/* -----------------------------------------------------------------------------
 * Camera Configuration
 */

class RCar4CameraConfiguration final : public CameraConfiguration
{
public:
	RCar4CameraConfiguration(RCar4CameraData *data);

	Status validate() override;

	const V4L2SubdeviceFormat &sensorFormat() { return sensorFormat_; }
	const Transform &combinedTransform() { return combinedTransform_; }
	const PixelFormat &ispOutputFormat() { return ispOutputFormat_; }

private:
	std::shared_ptr<RCar4CameraData> data_;

	V4L2SubdeviceFormat sensorFormat_;
	Transform combinedTransform_;
	PixelFormat ispOutputFormat_;
};

RCar4CameraConfiguration::RCar4CameraConfiguration(RCar4CameraData *data)
	: CameraConfiguration(), data_(data->_o<Camera>()->shared_from_this(), data)
{
}

CameraConfiguration::Status RCar4CameraConfiguration::validate()
{
	if (config_.empty())
		return Invalid;

	if (sensorConfig) {
		LOG(RCar4, Error)
			<< "Setting sensor configuration is not implemented";
		return Invalid;
	}

	Status status = validateColorSpaces(ColorSpaceFlag::StreamsShareColorSpace);

	/* Cap the number of entries to the available streams. */
	if (config_.size() > 2) {
		config_.resize(2);
		status = Adjusted;
	}

	Orientation requestedOrientation = orientation;
	combinedTransform_ = data_->vin_.sensor()->computeTransform(&orientation);
	if (orientation != requestedOrientation)
		status = Adjusted;

	StreamConfiguration *rawCfg = nullptr;
	StreamConfiguration *processedCfg = nullptr;

	for (size_t i = 0; i < config_.size(); i++) {
		StreamConfiguration &cfg = config_.at(i);
		const PixelFormatInfo &info = PixelFormatInfo::info(cfg.pixelFormat);

		if (info.colourEncoding == PixelFormatInfo::ColourEncodingRAW) {
			if (rawCfg) {
				LOG(RCar4, Error)
					<< "Camera configuration supports only one RAW stream";
				return Invalid;
			}

			rawCfg = &cfg;
		} else {
			if (processedCfg) {
				LOG(RCar4, Error)
					<< "Camera configuration supports only one processed stream";
				return Invalid;
			}

			processedCfg = &cfg;
		}

		if (cfg.bufferCount == 0) {
			cfg.bufferCount = kDefaultBufferCount;
			status = Adjusted;
		}
	}

	ASSERT(rawCfg || processedCfg);

	auto [sensorFormat, sensorCode, sensorSize] = data_->findSensorFormat(
		rawCfg ? rawCfg->pixelFormat : PixelFormat{},
		rawCfg ? rawCfg->size : processedCfg->size,
		combinedTransform_);

	V4L2DeviceFormat vinFormat = {};
	const auto vinPf = data_->vin_.output()->toV4L2PixelFormat(sensorFormat);
	vinFormat.fourcc = vinPf;
	vinFormat.size = sensorSize;

	if (data_->vin_.output()->tryFormat(&vinFormat))
		return Invalid;

	/* The format is expected to be accepted without adjustments. */
	if (vinFormat.fourcc != vinPf || vinFormat.size != sensorSize)
		return Invalid;

	ispOutputFormat_ = data_->outputFormats_.begin()->first;
	sensorFormat_ = {
		.code = sensorCode,
		.size = sensorSize,
		.colorSpace = ColorSpace::Raw,
	};

	if (rawCfg) {
		if (rawCfg->pixelFormat != sensorFormat)
			status = Adjusted;
		if (rawCfg->size != sensorSize)
			status = Adjusted;

		rawCfg->pixelFormat = sensorFormat;
		rawCfg->size = vinFormat.size;
		rawCfg->stride = vinFormat.planes[0].bpl;
		rawCfg->frameSize = vinFormat.planes[0].size;
		rawCfg->colorSpace = vinFormat.colorSpace;
		rawCfg->setStream(&data_->frames_.rawStream_);
	}

	if (processedCfg) {
		V4L2DeviceFormat ispFormat = {};
		ispFormat.fourcc = data_->isp_.output_->toV4L2PixelFormat(
			processedCfg->pixelFormat);
		ispFormat.size = sensorSize;

		if (data_->isp_.output_->tryFormat(&ispFormat))
			return Invalid;

		auto pf = ispFormat.fourcc.toPixelFormat();
		if (!pf.isValid())
			return Invalid;

		if (ispFormat.size != vinFormat.size)
			return Invalid;

		if (processedCfg->pixelFormat != pf)
			status = Adjusted;
		if (processedCfg->size != ispFormat.size)
			status = Adjusted;

		processedCfg->pixelFormat = pf;
		processedCfg->size = ispFormat.size;
		processedCfg->stride = ispFormat.planes[0].bpl;
		processedCfg->frameSize = ispFormat.planes[0].size;
		processedCfg->colorSpace = ispFormat.colorSpace;
		processedCfg->setStream(&data_->frames_.outputStream_);

		ispOutputFormat_ = processedCfg->pixelFormat;
	}

	return status;
}

/* -----------------------------------------------------------------------------
 * Pipeline Handler
 */

class PipelineHandlerRCar4 final : public PipelineHandler
{
public:
	PipelineHandlerRCar4(CameraManager *manager);

	std::unique_ptr<CameraConfiguration> generateConfiguration(Camera *camera,
								   std::span<const StreamRole> roles) override;
	int configure(Camera *camera, CameraConfiguration *config) override;

	int exportFrameBuffers(Camera *camera, Stream *stream,
			       std::vector<std::unique_ptr<FrameBuffer>> *buffers) override;

	int start(Camera *camera, const ControlList *controls) override;
	void stopDevice(Camera *camera) override;

	int queueRequestDevice(Camera *camera, Request *request) override;

	bool match(DeviceEnumerator *enumerator) override;

private:
	RCar4CameraData *cameraData(Camera *camera)
	{
		return static_cast<RCar4CameraData *>(camera->_d());
	}

	int createCamera(const MediaDevice *mdev, const std::string &pipeId);
};

PipelineHandlerRCar4::PipelineHandlerRCar4(CameraManager *manager)
	: PipelineHandler(manager, kMaxRequests)
{
}

std::unique_ptr<CameraConfiguration>
PipelineHandlerRCar4::generateConfiguration(Camera *camera,
					    std::span<const StreamRole> roles)
{
	RCar4CameraData *data = cameraData(camera);
	auto config = std::make_unique<RCar4CameraConfiguration>(data);

	if (roles.empty())
		return config;

	auto [sensorFormat, sensorCode, sensorSize] = data->findSensorFormat(
		{}, { -1u, -1u }, Transform::Identity);

	for (const StreamRole role : roles) {
		std::map<PixelFormat, std::vector<SizeRange>> formats;
		std::optional<ColorSpace> colorSpace;
		PixelFormat pixelFormat;

		switch (role) {
		case StreamRole::Raw:
			for (const auto &[mbusCode, sizes] : data->rawFormats_) {
				auto pf = BayerFormat::fromMbusCode(mbusCode).toPixelFormat();
				ASSERT(pf.isValid());
				formats.try_emplace(pf, sizes.begin(), sizes.end());
			}

			pixelFormat = sensorFormat;
			colorSpace = ColorSpace::Raw;
			break;
		default: {
			for (const auto &[pf, sizes] : data->outputFormats_)
				formats.try_emplace(pf, sizes.begin(), sizes.end());

			pixelFormat = formats.begin()->first;
			colorSpace = ColorSpace::Rec709;
			break;
		}
		}

		ASSERT(!formats.empty());
		StreamConfiguration cfg(StreamFormats{ formats });

		cfg.pixelFormat = pixelFormat;
		cfg.size = sensorSize;
		cfg.colorSpace = colorSpace;

		config->addConfiguration(cfg);
	}

	if (config->validate() == CameraConfiguration::Invalid)
		return {};

	return config;
}

int PipelineHandlerRCar4::configure(Camera *camera, CameraConfiguration *c)
{
	RCar4CameraConfiguration *config = static_cast<RCar4CameraConfiguration *>(c);
	RCar4CameraData *data = cameraData(camera);

	V4L2DeviceFormat vinFormat;
	int ret;

	/* Configure VIN and propagate format to ISP. */
	ret = data->vin_.configure(config->sensorFormat(),
				   config->combinedTransform(), &vinFormat);
	if (ret)
		return ret;

	ret = data->isp_.configure(vinFormat, config->ispOutputFormat());
	if (ret)
		return ret;

	/* Inform IPA of stream configuration and sensor controls. */
	IPACameraSensorInfo sensorInfo;
	ret = data->vin_.sensor()->sensorInfo(&sensorInfo);
	if (ret)
		return ret;

	ipa::rppx1::IPAConfigInfo ipaConfig{
		std::move(sensorInfo),
		data->vin_.sensor()->controls(),
	};

	ret = data->ipa_->configure(std::move(ipaConfig), &data->ipaControls_);
	if (ret) {
		LOG(RCar4, Error) << "failed configuring IPA (" << ret << ")";
		return ret;
	}

	data->updateControls();

	return 0;
}

int PipelineHandlerRCar4::exportFrameBuffers(Camera *camera, Stream *stream,
					     std::vector<std::unique_ptr<FrameBuffer>> *buffers)
{
	RCar4CameraData *data = cameraData(camera);
	unsigned int count = stream->configuration().bufferCount;

	if (stream == &data->frames_.outputStream_)
		return data->isp_.output_->exportBuffers(count, buffers);

	if (stream == &data->frames_.rawStream_)
		return data->isp_.input_->exportBuffers(count, buffers);

	return -EINVAL;
}

int PipelineHandlerRCar4::start(Camera *camera,
				[[maybe_unused]] const ControlList *controls)
{
	utils::scope_exit stopGuard([&] { stop(camera); });
	RCar4CameraData *data = cameraData(camera);

	data->delayedCtrls_->reset();

	int ret = data->frames_.start(&data->isp_, data->ipa_.get(), kMaxRequests);
	if (ret)
		return ret;

	ret = data->vin_.start(kMaxRequests);
	if (ret)
		return ret;

	ret = data->isp_.start(kMaxRequests);
	if (ret)
		return ret;

	ret = data->ipa_->start();
	if (ret)
		return ret;

	stopGuard.release();
	return 0;
}

void PipelineHandlerRCar4::stopDevice(Camera *camera)
{
	RCar4CameraData *data = cameraData(camera);

	data->ipa_->stop();
	data->isp_.stop();
	data->vin_.stop();

	data->frames_.stop(&data->isp_, data->ipa_.get());
}

int PipelineHandlerRCar4::queueRequestDevice(Camera *camera, Request *request)
{
	RCar4CameraData *data = cameraData(camera);

	RCar4Frames::Info *info = data->frames_.create(request);

	/* Always expected to have buffers for `kMaxRequests` in-flight requests. */
	ASSERT(info);

	int ret = data->vin_.queueBuffer(info->inputBuffer);
	if (ret) {
		data->frames_.remove(info);
		return ret;
	}

	data->ipa_->queueRequest(info->frame, request->controls());

	return 0;
}

int PipelineHandlerRCar4::createCamera(const MediaDevice *mdev,
				       const std::string &pipeId)
{
	auto data = std::make_unique<RCar4CameraData>(this);

	int ret = data->init(mdev, pipeId);
	if (ret)
		return ret;

	const std::string &id = data->vin_.sensor()->id();
	std::set<Stream *> streams{
		&data->frames_.rawStream_,
		&data->frames_.outputStream_,
	};

	registerCamera(Camera::create(std::move(data), id, streams));

	return 0;
}

bool PipelineHandlerRCar4::match(DeviceEnumerator *enumerator)
{
	DeviceMatch dm("rcar_vin");

	auto media = acquireMediaDevice(enumerator, dm);
	if (!media)
		return false;

	bool registered = false;
	for (const MediaEntity *entity : media->entities()) {
		if (!entity->name().starts_with("rcar_isp"))
			continue;
		if (entity->name().rfind("core") == std::string::npos)
			continue;

		/*
		 * Isolate the unit address that identifies one ISP
		 * instance. pipeId will look like
		 * 'rcar_isp fed00000.isp'.
		 */
		constexpr size_t prefix =
			std::string_view("rcar_isp fed00000.isp").length();

		std::string pipeId = entity->name().substr(0, prefix);
		if (!createCamera(media.get(), pipeId))
			registered = true;
	}

	return registered;
}

REGISTER_PIPELINE_HANDLER(PipelineHandlerRCar4, "rcar-gen4")

} /* namespace libcamera */
