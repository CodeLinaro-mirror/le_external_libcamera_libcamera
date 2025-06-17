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

#include <linux/rkisp1-config.h>

#include <libcamera/formats.h>
#include <libcamera/stream.h>

#include <libcamera/ipa/core_ipa_interface.h>
#include <libcamera/ipa/rkisp1_ipa_interface.h>
#include <libcamera/ipa/rkisp1_ipa_proxy.h>

#include "libcamera/internal/camera.h"
#include "libcamera/internal/camera_sensor.h"
#include "libcamera/internal/delayed_controls.h"
#include "libcamera/internal/device_enumerator.h"
#include "libcamera/internal/framebuffer.h"
#include "libcamera/internal/ipa_manager.h"
#include "libcamera/internal/mapped_framebuffer.h"
#include "libcamera/internal/media_device.h"
#include "libcamera/internal/pipeline_handler.h"
#include "libcamera/internal/v4l2_device.h"
#include "libcamera/internal/v4l2_subdevice.h"
#include "libcamera/internal/v4l2_videodevice.h"

#include "frames.h"
#include "isp.h"
#include "vin.h"

namespace libcamera {

namespace {

const std::map<PixelFormat, uint32_t> formatToMediaBus = {
	{ formats::SBGGR10, MEDIA_BUS_FMT_SBGGR10_1X10 },
	{ formats::SGBRG10, MEDIA_BUS_FMT_SGBRG10_1X10 },
	{ formats::SGRBG10, MEDIA_BUS_FMT_SGRBG10_1X10 },
	{ formats::SRGGB10, MEDIA_BUS_FMT_SRGGB10_1X10 },
};

/* Max supported resolution of VIN and ISP. */
static constexpr Size MaxResolution = { 4096, 4096 };
static constexpr unsigned int nBuffers = 4;

} /* namespace */

LOG_DEFINE_CATEGORY(RCar4)

/* -----------------------------------------------------------------------------
 * Camera Data
 */

class RCar4CameraData : public Camera::Private
{
public:
	RCar4CameraData(PipelineHandler *pipe)
		: Camera::Private(pipe)
	{
	}

	RCarVINDevice vin_;
	RCarISPDevice isp_;
	std::unique_ptr<ipa::rkisp1::IPAProxyRkISP1> ipa_;

	RCar4Frames frames_;
	std::unique_ptr<DelayedControls> delayedCtrls_;
	ControlInfoMap ipaControls_;

	void queuePendingRequests();
	void cancelPendingRequests();

	/* Requests for which no buffer has been queued to the VIN device yet. */
	std::queue<Request *> pendingRequests_;

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
};

void RCar4CameraData::queuePendingRequests()
{
	while (!pendingRequests_.empty()) {
		Request *request = pendingRequests_.front();

		RCar4Frames::Info *info = frames_.create(request);
		if (!info)
			break;

		if (vin_.queueBuffer(info->inputBuffer)) {
			/* Remove if raw buffer failed, should not happen. */
			frames_.remove(info);
			break;
		}

		ipa_->queueRequest(info->frame, request->controls());

		pendingRequests_.pop();
	}
}

void RCar4CameraData::cancelPendingRequests()
{
	while (!pendingRequests_.empty()) {
		Request *request = pendingRequests_.front();

		for (auto it : request->buffers()) {
			FrameBuffer *buffer = it.second;
			buffer->_d()->cancel();
			pipe()->completeBuffer(request, buffer);
		}

		pipe()->completeRequest(request);
		pendingRequests_.pop();
	}
}

void RCar4CameraData::vinBufferReady(FrameBuffer *buffer)
{
	RCar4Frames::Info *info = frames_.find(buffer);
	if (!info)
		return;

	Request *request = info->request;

	/* If the buffer is cancelled force a complete of the whole request. */
	if (buffer->metadata().status == FrameMetadata::FrameCancelled) {
		for (auto it : request->buffers()) {
			FrameBuffer *b = it.second;
			b->_d()->cancel();
			pipe()->completeBuffer(request, b);
		}

		frames_.remove(info);
		pipe()->completeRequest(request);
		return;
	}

	/* Record the sensor's timestamp in the request metadata. */
	request->metadata().set(controls::SensorTimestamp,
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

	request->metadata().set(controls::draft::PipelineDepth, 3);

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

	info->request->metadata().merge(metadata);
	info->metadataProcessed = true;

	if (frames_.tryComplete(info))
		pipe()->completeRequest(request);
}

/* -----------------------------------------------------------------------------
 * Camera Configuration
 */

class RCar4CameraConfiguration : public CameraConfiguration
{
public:
	RCar4CameraConfiguration(Camera *camera, RCar4CameraData *data);

	Status validate() override;

	const V4L2SubdeviceFormat &sensorFormat() { return sensorFormat_; }
	const Transform &combinedTransform() { return combinedTransform_; }
	const PixelFormat &ispOutputFormat() { return ispOutputFormat_; }
private:
	std::shared_ptr<Camera> camera_;
	RCar4CameraData *data_;

	V4L2SubdeviceFormat sensorFormat_;
	Transform combinedTransform_;
	PixelFormat ispOutputFormat_;
};

RCar4CameraConfiguration::RCar4CameraConfiguration(Camera *camera,
						   RCar4CameraData *data)
	: CameraConfiguration()
{
	camera_ = camera->shared_from_this();
	data_ = data;
}

CameraConfiguration::Status RCar4CameraConfiguration::validate()
{
	Status status;

	if (config_.empty())
		return Invalid;

	status = validateColorSpaces(ColorSpaceFlag::StreamsShareColorSpace);

	/* Cap the number of entries to the available streams. */
	if (config_.size() > 2) {
		config_.resize(2);
		status = Adjusted;
	}

	Orientation requestedOrientation = orientation;
	combinedTransform_ = data_->vin_.sensor()->computeTransform(&orientation);
	if (orientation != requestedOrientation)
		status = Adjusted;

	/* Figure out the VIN configuration based on the first stream size. */
	StreamConfiguration vinCfg = data_->vin_.generateConfiguration(config_.at(0).size);

	/* Default ISP output format. */
	ispOutputFormat_ = formats::XRGB8888;

	/*
	 * Validate there are at max two streams, one output and one RAW. The
	 * size of two streams must match each other and the sensor output as we
	 * have no scaler.
	 */
	unsigned int outputStreams = 0;
	unsigned int rawStreams = 0;
	for (unsigned int i = 0; i < config_.size(); i++) {
		StreamConfiguration &cfg = config_.at(i);
		StreamConfiguration newCfg = {};
		const StreamConfiguration originalCfg = cfg;
		const PixelFormatInfo &info = PixelFormatInfo::info(cfg.pixelFormat);

		LOG(RCar4, Debug) << "Validating stream: " << cfg.toString();

		if (info.colourEncoding == PixelFormatInfo::ColourEncodingRAW) {
			if (rawStreams++) {
				LOG(RCar4, Error) <<
					"Camera configuration support only one RAW stream";
				return Invalid;
			}

			newCfg = vinCfg;

			cfg.setStream(&data_->frames_.rawStream_);
			LOG(RCar4, Debug) << "Assigned " << newCfg.toString()
				<< " to the raw stream";
		} else {
			if (outputStreams++) {
				LOG(RCar4, Error) <<
					"Camera configuration support only one output stream";
				return Invalid;
			}

			newCfg = data_->isp_.generateConfiguration(cfg.pixelFormat, vinCfg.size);
			ispOutputFormat_ = newCfg.pixelFormat;

			cfg.setStream(&data_->frames_.outputStream_);
			LOG(RCar4, Debug) << "Assigned " << newCfg.toString()
				<< " to the output stream";
		}

		cfg.size = newCfg.size;
		cfg.bufferCount = newCfg.bufferCount;
		cfg.pixelFormat = newCfg.pixelFormat;
		cfg.stride = newCfg.stride;
		cfg.frameSize = newCfg.frameSize;

		if (!cfg.pixelFormat.isValid()) {
			LOG(RCar4, Error)
				<< "Stream " << i << " can not generate cfg";
			return Invalid;
		}

		if (cfg.pixelFormat != originalCfg.pixelFormat ||
		    cfg.size != originalCfg.size) {
			LOG(RCar4, Debug)
				<< "Stream " << i << " configuration adjusted to "
				<< cfg.toString();
			status = Adjusted;
		}
	}

	/* Select the sensor format. */
	sensorFormat_ =
		data_->vin_.sensor()->getFormat({ formatToMediaBus.at(vinCfg.pixelFormat) },
						vinCfg.size, vinCfg.size);

	return status;
}

/* -----------------------------------------------------------------------------
 * Pipeline Handler
 */

class PipelineHandlerRCar4 : public PipelineHandler
{
public:
	PipelineHandlerRCar4(CameraManager *manager);

	std::unique_ptr<CameraConfiguration> generateConfiguration(Camera *camera,
								   Span<const StreamRole> roles) override;
	int configure(Camera *camera, CameraConfiguration *config) override;

	int exportFrameBuffers(Camera *camera, Stream *stream,
			       std::vector<std::unique_ptr<FrameBuffer>> *buffers) override;

	int start(Camera *camera, const ControlList *controls) override;
	void stopDevice(Camera *camera) override;

	int queueRequestDevice(Camera *camera, Request *request) override;

	bool match(DeviceEnumerator *enumerator) override;

	int updateControls(RCar4CameraData *data);

private:
	RCar4CameraData *cameraData(Camera *camera)
	{
		return static_cast<RCar4CameraData *>(camera->_d());
	}

	int createCamera(MediaDevice *mdev, const std::string &pipeId);

	StreamConfiguration generateStreamConfiguration(RCar4CameraData *data,
							StreamRole role);
};

PipelineHandlerRCar4::PipelineHandlerRCar4(CameraManager *manager)
	: PipelineHandler(manager)
{
}

StreamConfiguration
PipelineHandlerRCar4::generateStreamConfiguration(RCar4CameraData *data,
						  StreamRole role)
{
	const std::vector<unsigned int> &mbusCodes = data->vin_.sensor()->mbusCodes();

	/* Create the list of supported RAW stream formats. */
	std::map<PixelFormat, std::vector<SizeRange>> rawFormats;
	unsigned int rawBitsPerPixel = 0;
	PixelFormat rawFormat;
	Size rawSize = { 0, 0 };
	std::vector<SizeRange> rawSizes;

	for (const auto &format : data->vin_.formats()) {
		const PixelFormatInfo &info = PixelFormatInfo::info(format);

		/* Populate stream formats for RAW configurations. */
		uint32_t mbusCode = formatToMediaBus.at(format);

		/* Skip formats not supported by sensor. */
		if (std::find(mbusCodes.begin(), mbusCodes.end(), mbusCode) == mbusCodes.end())
			continue;

		/* Add all the RAW sizes the sensor can produce for this code. */
		for (const auto &rawSizeByCode : data->vin_.sensor()->sizes(mbusCode)) {
			if (rawSizeByCode.width > MaxResolution.width ||
			    rawSizeByCode.height > MaxResolution.height)
				continue;

			rawSizes.push_back({ rawSizeByCode, rawSizeByCode });

			rawFormats[format].push_back({ rawSizeByCode, rawSizeByCode });

			/* Cache for later default format. */
			if (info.bitsPerPixel >= rawBitsPerPixel) {
				rawBitsPerPixel = info.bitsPerPixel;
				rawFormat = format;

				if (rawSizeByCode > rawSize)
					rawSize = rawSizeByCode;
			}
		}
	}

	/* If generating for RAW role we are done. */
	if (role == StreamRole::Raw) {
		StreamFormats rawStreamFormats(rawFormats);
		StreamConfiguration rawCfg(rawStreamFormats);
		rawCfg.pixelFormat = rawFormat;
		rawCfg.size = rawSize;
		rawCfg.bufferCount = nBuffers;

		return rawCfg;
	}

	/* Create the list of supported other stream formats. */
	std::map<PixelFormat, std::vector<SizeRange>> outputFormats;
	std::vector<SizeRange> outputSizes(rawSizes.begin(), rawSizes.end());

	for (const auto &format : data->isp_.formats()) {
		const PixelFormatInfo &info = PixelFormatInfo::info(format);

		/* Skip RAW formats. */
		if (info.colourEncoding == PixelFormatInfo::ColourEncodingRAW)
			continue;

		outputFormats[format] = { outputSizes };
	}

	StreamFormats outputStreamFormats(outputFormats);
	StreamConfiguration outputCfg(outputStreamFormats);
	outputCfg.pixelFormat = formats::XRGB8888;
	outputCfg.size = rawSize;

	return outputCfg;
}

std::unique_ptr<CameraConfiguration>
PipelineHandlerRCar4::generateConfiguration(Camera *camera,
					    Span<const StreamRole> roles)
{
	RCar4CameraData *data = cameraData(camera);

	std::unique_ptr<CameraConfiguration> config =
		std::make_unique<RCar4CameraConfiguration>(camera, data);

	if (roles.empty())
		return config;

	for (const StreamRole role : roles) {
		std::optional<ColorSpace> colorSpace;

		switch (role) {
		case StreamRole::Raw:
			colorSpace = ColorSpace::Raw;
			break;
		default:
			colorSpace = ColorSpace::Rec709;
			break;
		}

		StreamConfiguration cfg =
			generateStreamConfiguration(data, role);
		if (!cfg.pixelFormat.isValid())
			return nullptr;

		cfg.colorSpace = colorSpace;
		cfg.bufferCount = nBuffers;
		config->addConfiguration(cfg);
	}

	if (config->validate() == CameraConfiguration::Invalid)
		return nullptr;

	return config;
}

int PipelineHandlerRCar4::configure(Camera *camera, CameraConfiguration *c)
{
	RCar4CameraConfiguration *config
		= static_cast<RCar4CameraConfiguration *>(c);
	RCar4CameraData *data = cameraData(camera);

	V4L2DeviceFormat vinFormat;
	int ret;

	/* Configure VIN and propagate format to ISP. */
	ret = data->vin_.configure(config->sensorFormat().size,
				   config->combinedTransform(), &vinFormat);
	if (ret)
		return ret;

	ret = data->isp_.configure(&vinFormat, config->ispOutputFormat());
	if (ret)
		return ret;

	/* Inform IPA of stream configuration and sensor controls. */
	IPACameraSensorInfo sensorInfo;
	ret = data->vin_.sensor()->sensorInfo(&sensorInfo);
	if (ret)
		return ret;

	ipa::rkisp1::IPAConfigInfo ipaConfig{ sensorInfo,
		data->vin_.sensor()->controls(),
		V4L2_META_FMT_RK_ISP1_EXT_PARAMS };

	std::map<unsigned int, IPAStream> streamConfig;
	streamConfig[0] =
		IPAStream(PixelFormat(config->ispOutputFormat().fourcc()),
			  config->sensorFormat().size);

	ret = data->ipa_->configure(ipaConfig, streamConfig, &data->ipaControls_);
	if (ret) {
		LOG(RCar4, Error) << "failed configuring IPA (" << ret << ")";
		return ret;
	}

	return updateControls(data);
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
	RCar4CameraData *data = cameraData(camera);
	int ret;

	data->delayedCtrls_->reset();

	ret = data->frames_.start(&data->isp_, data->ipa_.get());
	if (ret)
		goto error;

	ret = data->ipa_->start();
	if (ret)
		goto error;

	ret = data->vin_.start();
	if (ret)
		goto error;

	ret = data->isp_.start();
	if (ret)
		goto error;

	return 0;
error:
	stop(camera);

	return ret;
}

void PipelineHandlerRCar4::stopDevice(Camera *camera)
{
	RCar4CameraData *data = cameraData(camera);

	data->cancelPendingRequests();

	data->isp_.stop();
	data->vin_.stop();
	data->ipa_->stop();

	data->frames_.stop(&data->isp_, data->ipa_.get());
}

int PipelineHandlerRCar4::queueRequestDevice(Camera *camera, Request *request)
{
	RCar4CameraData *data = cameraData(camera);

	data->pendingRequests_.push(request);
	data->queuePendingRequests();

	return 0;
}

int PipelineHandlerRCar4::updateControls(RCar4CameraData *data)
{
	ControlInfoMap::Map controls;

	for (const auto &ipaControl : data->ipaControls_)
		controls[ipaControl.first] = ipaControl.second;

	data->controlInfo_ = ControlInfoMap(std::move(controls),
					    controls::controls);
	return 0;
}

int PipelineHandlerRCar4::createCamera(MediaDevice *mdev,
				       const std::string &pipeId)
{
	std::unique_ptr<RCar4CameraData> data = std::make_unique<RCar4CameraData>(this);
	IPACameraSensorInfo sensorInfo{};
	int ret;

	ret = data->vin_.init(mdev, pipeId);
	if (ret)
		return ret;

	ret = data->isp_.init(mdev, pipeId);
	if (ret)
		return ret;

	/*
	 * Load RkISP1 IPA for use with RCar4
	 */
	data->ipa_ = IPAManager::createIPA<ipa::rkisp1::IPAProxyRkISP1>(this, 1, 1, "rkisp1");
	if (!data->ipa_) {
		LOG(RCar4, Error) << "No IPA module found";
		return -ENOENT;
	}

	/* The IPA tuning file is made from the sensor name. */
	std::string ipaTuningFile =
		data->ipa_->configurationFile(data->vin_.sensor()->model() + ".yaml", "uncalibrated.yaml");

	ret = data->vin_.sensor()->sensorInfo(&sensorInfo);
	if (ret) {
		LOG(RCar4, Error) << "Camera sensor information not available";
		return ret;
	}

	ret = data->ipa_->init({ ipaTuningFile, data->vin_.sensor()->model() },
			       libcamera::ipa::rkisp1::HwRevisionExternalRppX1,
			       sensorInfo, data->vin_.sensor()->controls(), &data->ipaControls_);
	if (ret < 0) {
		LOG(RCar4, Error) << "IPA initialization failure";
		return ret;
	}

	updateControls(data.get());

	/*
	 * Initialize the camera properties.
	 */
	data->properties_ = data->vin_.sensor()->properties();
	const CameraSensorProperties::SensorDelays &delays = data->vin_.sensor()->sensorDelays();
	std::unordered_map<uint32_t, DelayedControls::ControlParams> params = {
		{ V4L2_CID_ANALOGUE_GAIN, { delays.gainDelay, false } },
		{ V4L2_CID_EXPOSURE, { delays.exposureDelay, false } },
		{ V4L2_CID_VBLANK, { delays.vblankDelay, false } },
	};

	data->delayedCtrls_ =
		std::make_unique<DelayedControls>(data->vin_.sensor()->device(),
						  params);

	std::set<Stream *> streams{
		&data->frames_.rawStream_,
		&data->frames_.outputStream_,
	};

	/*
	 * Connect signals to slots to drive the pipeline.
	 */

	/* When internal buffers become available try to queue more jobs. */
	data->frames_.bufferAvailable.connect(data.get(),
					      &RCar4CameraData::queuePendingRequests);

	/* Connect bufferReady for each video device to a handler. */
	data->vin_.bufferReady().connect(data.get(),
					 &RCar4CameraData::vinBufferReady);
	data->isp_.input_->bufferReady.connect(data.get(),
					       &RCar4CameraData::inputBufferReady);
	data->isp_.param_->bufferReady.connect(data.get(),
					       &RCar4CameraData::paramBufferReady);
	data->isp_.stat_->bufferReady.connect(data.get(),
					      &RCar4CameraData::statBufferReady);
	data->isp_.output_->bufferReady.connect(data.get(),
						&RCar4CameraData::outputBufferReady);

	/* Connect IPA signals. */
	data->ipa_->setSensorControls.connect(data.get(),
					      &RCar4CameraData::setSensorControls);
	data->ipa_->paramsComputed.connect(data.get(),
					   &RCar4CameraData::paramsComputed);
	data->ipa_->metadataReady.connect(data.get(),
					  &RCar4CameraData::metadataReady);

	/* Apply controls at start at exposure. */
	data->vin_.frameStart().connect(data->delayedCtrls_.get(),
					&DelayedControls::applyControls);

	/*
	 * Register the camera.
	 */
	const std::string id = data->vin_.sensor()->entity()->name();
	std::shared_ptr<Camera> camera = Camera::create(std::move(data), id, streams);

	registerCamera(std::move(camera));

	return 0;
}

bool PipelineHandlerRCar4::match(DeviceEnumerator *enumerator)
{
	DeviceMatch dm("rcar_vin");

	MediaDevice *media = acquireMediaDevice(enumerator, dm);
	if (!media)
		return false;

	bool registered = false;
	for (const MediaEntity *entity : media->entities()) {
		if (entity->name().substr(0, 8) == "rcar_isp" &&
		    entity->name().rfind("core") != std::string::npos) {
			/*
			 * Isolate the unit address that identifies one ISP
			 * instance. pipeId will look like
			 * 'rcar_isp fed00000.isp'.
			 */
			std::string pipeId = entity->name().substr(0, 21);
			if (!createCamera(media, pipeId))
				registered = true;
		}
	}

	return registered;
}

REGISTER_PIPELINE_HANDLER(PipelineHandlerRCar4, "rcar-gen4")

} /* namespace libcamera */
