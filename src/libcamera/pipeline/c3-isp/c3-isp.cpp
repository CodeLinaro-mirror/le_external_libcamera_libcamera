/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * Copyright (C) 2024, Amlogic Inc.
 *
 * Pipeline Handler for Amlogic C3 ISP
 */

#include <algorithm>
#include <array>
#include <map>
#include <memory>
#include <queue>
#include <set>
#include <string>

#include <linux/media-bus-format.h>
#include <linux/media.h>

#include <libcamera/base/log.h>

#include <libcamera/camera.h>
#include <libcamera/formats.h>
#include <libcamera/geometry.h>
#include <libcamera/stream.h>

#include <libcamera/ipa/c3-isp_ipa_interface.h>
#include <libcamera/ipa/c3-isp_ipa_proxy.h>
#include <libcamera/ipa/core_ipa_interface.h>

#include "libcamera/internal/bayer_format.h"
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

namespace {

bool isFmtRaw(const libcamera::PixelFormat &pixFmt)
{
	return libcamera::PixelFormatInfo::info(pixFmt).colourEncoding ==
	       libcamera::PixelFormatInfo::ColourEncodingRAW;
}

} /* namespace */

namespace libcamera {

LOG_DEFINE_CATEGORY(C3ISP)

const std::map<libcamera::PixelFormat, unsigned int> C3ISPFmtToCode = {
	{ formats::R8, MEDIA_BUS_FMT_YUV10_1X30 },
	{ formats::NV12, MEDIA_BUS_FMT_YUV10_1X30 },
	{ formats::NV21, MEDIA_BUS_FMT_YUV10_1X30 },
	{ formats::NV16, MEDIA_BUS_FMT_YUV10_1X30 },
	{ formats::NV61, MEDIA_BUS_FMT_YUV10_1X30 },

	/* RAW formats, STILL pipe only. */
	{ formats::SRGGB12, MEDIA_BUS_FMT_SRGGB16_1X16 },
	{ formats::SBGGR12, MEDIA_BUS_FMT_SBGGR16_1X16 },
	{ formats::SGRBG12, MEDIA_BUS_FMT_SGRBG16_1X16 },
	{ formats::SGBRG12, MEDIA_BUS_FMT_SGBRG16_1X16 },
};

constexpr Size kC3ISPMinSize = { 160, 120 };
constexpr Size kC3ISPMaxSize = { 2888, 2240 };

struct C3ISPFrameInfo {
	Request *request;

	FrameBuffer *paramBuffer;
	FrameBuffer *statBuffer;

	bool paramsDone;
	bool statsDone;
};

class C3ISPCameraData : public Camera::Private
{
public:
	C3ISPCameraData(PipelineHandler *pipe, MediaEntity *entity)
		: Camera::Private(pipe), entity_(entity)
	{
	}

	int init();
	int loadIPA();

	int pixfmtToMbusCode(const PixelFormat &pixFmt) const;
	const PixelFormat &bestRawFmt() const;

	PixelFormat adjustRawFmt(const PixelFormat &pixFmt) const;
	Size adjustRawSizes(const PixelFormat &pixFmt, const Size &rawSize) const;

	void updateControls(const ControlInfoMap &ipaControls);

	MediaEntity *entity_;
	std::unique_ptr<CameraSensor> sensor_;
	std::unique_ptr<V4L2Subdevice> csi_;
	std::unique_ptr<V4L2Subdevice> adap_;
	Stream viewStream;
	Stream stillStream;
	Stream videoStream;

	std::unique_ptr<ipa::c3isp::IPAProxyC3ISP> ipa_;
	std::vector<IPABuffer> ipaStatBuffers_;
	std::vector<IPABuffer> ipaParamBuffers_;

	std::unique_ptr<DelayedControls> delayedCtrls_;

private:
	void setSensorControls(const ControlList &sensorControls);
};

int C3ISPCameraData::init()
{
	/* Register a CameraSensor */
	sensor_ = CameraSensorFactoryBase::create(entity_);
	if (!sensor_)
		return -EINVAL;

	const MediaPad *sensorSrc = entity_->getPadByIndex(0);
	MediaEntity *csiEntity = sensorSrc->links()[0]->sink()->entity();

	csi_ = std::make_unique<V4L2Subdevice>(csiEntity);
	if (csi_->open()) {
		LOG(C3ISP, Error) << "Failed to open CSI-2 subdevice";
		return -EINVAL;
	}

	const MediaPad *csiSrc = csiEntity->getPadByIndex(1);
	MediaEntity *adapEntity = csiSrc->links()[0]->sink()->entity();

	adap_ = std::make_unique<V4L2Subdevice>(adapEntity);
	if (adap_->open()) {
		LOG(C3ISP, Error) << "Failed to open adapter subdevice";
		return -EINVAL;
	}

	return 0;
}

void C3ISPCameraData::setSensorControls(const ControlList &sensorControls)
{
	delayedCtrls_->push(sensorControls);
}

/*
 * Search the sensor's supported formats for the one with a matching bayer
 * order and the greatest bitdepth.
 */
int C3ISPCameraData::pixfmtToMbusCode(const PixelFormat &pixFmt) const
{
	auto it = C3ISPFmtToCode.find(pixFmt);
	if (it == C3ISPFmtToCode.end())
		return -EINVAL;

	BayerFormat bayerFmt = BayerFormat::fromMbusCode(it->second);
	if (!bayerFmt.isValid())
		return -EINVAL;

	unsigned int snsMbusCode = 0;
	unsigned int bitDepth = 0;
	for (const auto &code : sensor_->mbusCodes()) {
		BayerFormat snsBayerFmt = BayerFormat::fromMbusCode(code);
		if (!snsBayerFmt.isValid())
			continue;

		if (snsBayerFmt.order != bayerFmt.order)
			continue;

		if (snsBayerFmt.bitDepth > bitDepth) {
			bitDepth = snsBayerFmt.bitDepth;
			snsMbusCode = code;
		}
	}

	if (!snsMbusCode)
		return -EINVAL;

	return snsMbusCode;
}

/* Find a RAW PixelFormat supported by both the ISP and the sensor. */
const PixelFormat &C3ISPCameraData::bestRawFmt() const
{
	static const PixelFormat invalidPixFmt = {};

	for (const auto &code : sensor_->mbusCodes()) {
		BayerFormat sensorBayer = BayerFormat::fromMbusCode(code);

		if (!sensorBayer.isValid())
			continue;

		for (const auto &[pixFmt, rawCode] : C3ISPFmtToCode) {
			if (!isFmtRaw(pixFmt))
				continue;

			BayerFormat bayer = BayerFormat::fromMbusCode(rawCode);
			if (bayer.order == sensorBayer.order)
				return pixFmt;
		}
	}

	LOG(C3ISP, Error) << "Can't get a compatible format";

	return invalidPixFmt;
}

/*
 * Make sure the provided raw pixel format is supported and adjust it to
 * one of the supported ones if it's not.
 */
PixelFormat C3ISPCameraData::adjustRawFmt(const PixelFormat &rawFmt) const
{
	int rawCode = pixfmtToMbusCode(rawFmt);
	if (rawCode < 0)
		return bestRawFmt();

	const auto rawSizes = sensor_->sizes(rawCode);
	if (rawSizes.empty())
		return bestRawFmt();

	return rawFmt;
}

Size C3ISPCameraData::adjustRawSizes(const PixelFormat &rawFmt, const Size &size) const
{
	Size rawSize = size.boundedTo(kC3ISPMaxSize);

	int rawCode = pixfmtToMbusCode(rawFmt);
	if (rawCode < 0)
		return {};

	const auto rawSizes = sensor_->sizes(rawCode);

	auto sizeIt = std::find(rawSizes.begin(), rawSizes.end(), rawSize);
	if (sizeIt != rawSizes.end())
		return rawSize;

	/* Adjust the rawSize to the closest supported size */
	uint16_t distance = std::numeric_limits<uint16_t>::max();
	Size bestSize;
	for (const Size &sz : rawSizes) {
		uint16_t dist = std::abs(static_cast<int>(rawSize.width) -
					 static_cast<int>(sz.width)) +
				std::abs(static_cast<int>(rawSize.height) -
					 static_cast<int>(sz.height));
		if (dist < distance) {
			distance = dist;
			bestSize = sz;
		}
	}

	return bestSize;
}

void C3ISPCameraData::updateControls(const ControlInfoMap &ipaControls)
{
	ControlInfoMap::Map controls;

	for (auto const &c : ipaControls)
		controls.emplace(c.first, c.second);

	controlInfo_ = ControlInfoMap(std::move(controls), controls::controls);
}

int C3ISPCameraData::loadIPA()
{
	int ret;

	ipa_ = IPAManager::createIPA<ipa::c3isp::IPAProxyC3ISP>(pipe(), 1, 1);
	if (!ipa_)
		return -ENOENT;

	ipa_->setSensorControls.connect(this, &C3ISPCameraData::setSensorControls);

	std::string ipaTuningFile = ipa_->configurationFile(sensor_->model() + ".yaml",
							    "uncalibrated.yaml");

	ipa::c3isp::IPAConfigInfo ipaConfig{};

	ret = sensor_->sensorInfo(&ipaConfig.sensorInfo);
	if (ret)
		return ret;

	ipaConfig.sensorControls = sensor_->controls();

	ControlInfoMap ipaControls;
	ret = ipa_->init({ ipaTuningFile, sensor_->model() }, ipaConfig, &ipaControls);
	if (ret) {
		LOG(C3ISP, Error) << "Failed to initialize IPA";
		return ret;
	}

	updateControls(ipaControls);

	return 0;
}

class C3ISPCameraConfiguration : public CameraConfiguration
{
public:
	C3ISPCameraConfiguration(C3ISPCameraData *data)
		: CameraConfiguration(), data_(data)
	{
	}

	Status validate() override;
	const Transform &combinedTransform() { return combinedTransform_; }

	V4L2SubdeviceFormat sensorFormat_;

private:
	static constexpr unsigned int kMaxStreams = 3;

	const C3ISPCameraData *data_;
	Transform combinedTransform_;
};

CameraConfiguration::Status C3ISPCameraConfiguration::validate()
{
	Status status = Valid;

	if (config_.empty())
		return Invalid;

	if (config_.size() > kMaxStreams) {
		config_.resize(kMaxStreams);
		status = Adjusted;
	}

	Orientation requestOrientation = orientation;
	combinedTransform_ = data_->sensor_->computeTransform(&orientation);
	if (orientation != requestOrientation)
		status = Adjusted;

	bool stillPipeAvailable = true;
	StreamConfiguration *rawConfig = nullptr;
	for (StreamConfiguration &config : config_) {
		if (!isFmtRaw(config.pixelFormat))
			continue;

		if (rawConfig) {
			LOG(C3ISP, Error) << "Only support a RAW stream";
			return Invalid;
		}

		rawConfig = &config;
	}

	/*
	 * The C3 ISP can not upscale. Limit the configuration to the ISP
	 * capabilities and the sensor resolution.
	 */
	Size maxSize = kC3ISPMaxSize.boundedTo(data_->sensor_->resolution());
	if (rawConfig) {
		PixelFormat rawFmt =
			data_->adjustRawFmt(rawConfig->pixelFormat);

		if (!rawFmt.isValid())
			return Invalid;

		if (rawFmt != rawConfig->pixelFormat) {
			LOG(C3ISP, Debug)
				<< "Adjust RAW format to " << rawFmt;
			rawConfig->pixelFormat = rawFmt;
			status = Adjusted;
		}

		Size rawSize = data_->adjustRawSizes(rawFmt, rawConfig->size);
		if (rawSize != rawConfig->size) {
			LOG(C3ISP, Debug)
				<< "Adjust RAW size to " << rawSize;
			rawConfig->size = rawSize;
			status = Adjusted;
		}

		maxSize = rawSize;

		const PixelFormatInfo &info = PixelFormatInfo::info(rawConfig->pixelFormat);
		rawConfig->stride = info.stride(rawConfig->size.width, 0, 16);
		rawConfig->frameSize = info.frameSize(rawConfig->size, 4);

		rawConfig->setStream(const_cast<Stream *>(&data_->stillStream));
		stillPipeAvailable = false;
	}

	/* Adjust processed streams */

	bool videoPipeAvailable = true;
	Size maxConfigSize;
	for (StreamConfiguration &config : config_) {
		if (isFmtRaw(config.pixelFormat))
			continue;

		const auto it = C3ISPFmtToCode.find(config.pixelFormat);
		if (it == C3ISPFmtToCode.end()) {
			LOG(C3ISP, Debug)
				<< "Format adjusted to " << formats::NV12;
			config.pixelFormat = formats::NV12;
			status = Adjusted;
		}

		Size size = std::clamp(config.size, kC3ISPMinSize, maxSize);
		if (size != config.size) {
			LOG(C3ISP, Debug)
				<< "Size adjusted to " << size;
			config.size = size;
			status = Adjusted;
		}

		if (maxConfigSize < size)
			maxConfigSize = size;

		if (stillPipeAvailable) {
			config.setStream(const_cast<Stream *>(&data_->stillStream));
			stillPipeAvailable = false;
		} else if (videoPipeAvailable) {
			config.setStream(const_cast<Stream *>(&data_->videoStream));
			videoPipeAvailable = false;
		} else {
			config.setStream(const_cast<Stream *>(&data_->viewStream));
		}
	}

	/* Configure the sensor format */

	/* If there's a RAW config, sensor format follow it */
	if (rawConfig) {
		sensorFormat_.code = data_->pixfmtToMbusCode(rawConfig->pixelFormat);
		sensorFormat_.size = rawConfig->size;

		LOG(C3ISP, Debug) << "RAW configuration format " << sensorFormat_;

		return status;
	}

	/* If there's no RAW config, compture the sensor format */
	PixelFormat rawFormat = data_->bestRawFmt();
	if (!rawFormat.isValid())
		return Invalid;

	sensorFormat_.code = data_->pixfmtToMbusCode(rawFormat);
	sensorFormat_.size = data_->adjustRawSizes(rawFormat, maxConfigSize);

	LOG(C3ISP, Debug) << "Sensor format " << sensorFormat_;

	return status;
}

class PipelineHandlerC3ISP : public PipelineHandler
{
public:
	PipelineHandlerC3ISP(CameraManager *manager);

	std::unique_ptr<CameraConfiguration> generateConfiguration(Camera *camera,
								   Span<const StreamRole> roles) override;
	int configure(Camera *camera, CameraConfiguration *config) override;

	int exportFrameBuffers(Camera *camera, Stream *stream,
			       std::vector<std::unique_ptr<FrameBuffer>> *buffers) override;

	int allocateBuffers(Camera *camera);
	void freeBuffers(Camera *camera);

	int start(Camera *camera, const ControlList *controls) override;
	void stopDevice(Camera *camera) override;

	int queueRequestDevice(Camera *camera, Request *request) override;

	void imageBufferReady(FrameBuffer *buffer);
	void paramsBufferReady(FrameBuffer *buffer);
	void statsBufferReady(FrameBuffer *buffer);
	void paramsComputed(unsigned int requestId, unsigned int bytesused);
	void statsProcessed(unsigned int requestId, const ControlList &metadata);

	bool match(DeviceEnumerator *enumerator) override;

private:
	struct C3ISPPipe {
		std::unique_ptr<V4L2Subdevice> resizer;
		std::unique_ptr<V4L2VideoDevice> cap;
		Stream *stream;
	};

	enum {
		C3ISPVIEW,
		C3ISPVIDEO,
		C3ISPSTILL,
		C3ISPNumPipes,
	};

	C3ISPCameraData *cameraData(Camera *camera)
	{
		return static_cast<C3ISPCameraData *>(camera->_d());
	}

	C3ISPPipe *pipeFromStream(C3ISPCameraData *data, Stream *stream)
	{
		if (stream == &data->viewStream)
			return &pipes_[C3ISPVIEW];
		else if (stream == &data->stillStream)
			return &pipes_[C3ISPSTILL];
		else if (stream == &data->videoStream)
			return &pipes_[C3ISPVIDEO];
		else
			LOG(C3ISP, Fatal) << "Invalid stream: " << stream;

		return nullptr;
	}

	C3ISPPipe *pipeFromStream(C3ISPCameraData *data, const Stream *stream)
	{
		return pipeFromStream(data, const_cast<Stream *>(stream));
	}

	void resetPipes()
	{
		for (C3ISPPipe &pipe : pipes_)
			pipe.stream = nullptr;
	}

	int pipesStart();
	void pipesStop();

	int configureRawStream(C3ISPCameraData *data,
			       const StreamConfiguration &config,
			       V4L2SubdeviceFormat &subdevFormat);
	int configureProcessedStream(C3ISPCameraData *data,
				     const StreamConfiguration &config,
				     V4L2SubdeviceFormat &subdevFormat);

	bool createCamera(MediaLink *ispLink);
	void tryComplete(C3ISPFrameInfo *info);
	C3ISPFrameInfo *findFrameInfo(FrameBuffer *buffer);
	C3ISPFrameInfo *findFrameInfo(Request *request);
	void clearIncompleteRequests();

	MediaDevice *media_;
	std::unique_ptr<V4L2Subdevice> isp_;
	std::unique_ptr<V4L2VideoDevice> params_;
	std::unique_ptr<V4L2VideoDevice> stats_;

	std::vector<std::unique_ptr<FrameBuffer>> statsBuffers_;
	std::queue<FrameBuffer *> availableStatsBuffers_;

	std::vector<std::unique_ptr<FrameBuffer>> paramsBuffers_;
	std::queue<FrameBuffer *> availableParamsBuffers_;

	std::map<unsigned int, C3ISPFrameInfo> frameInfoMap_;

	std::array<C3ISPPipe, C3ISPNumPipes> pipes_;
};

PipelineHandlerC3ISP::PipelineHandlerC3ISP(CameraManager *manager)
	: PipelineHandler(manager)
{
}

std::unique_ptr<CameraConfiguration>
PipelineHandlerC3ISP::generateConfiguration(Camera *camera,
					    Span<const StreamRole> roles)
{
	C3ISPCameraData *data = cameraData(camera);
	std::unique_ptr<CameraConfiguration> config =
		std::make_unique<C3ISPCameraConfiguration>(data);
	bool isRoleRaw = false;

	if (roles.empty())
		return config;

	/* Reserve the C3ISPSTILL pipe for Raw role */
	if (std::find(roles.begin(), roles.end(), StreamRole::Raw) != roles.end())
		isRoleRaw = true;

	for (const StreamRole &role : roles) {
		struct C3ISPPipe *pipe;
		PixelFormat pixelFormat;
		Size size = std::min(Size{ 1920, 1080 }, data->sensor_->resolution());

		switch (role) {
		case StreamRole::StillCapture:
			size = data->sensor_->resolution();
			pixelFormat = formats::NV12;
			pipe = isRoleRaw ? nullptr : &pipes_[C3ISPSTILL];
			break;

		case StreamRole::VideoRecording:
			pixelFormat = formats::NV12;
			pipe = &pipes_[C3ISPVIDEO];
			break;

		case StreamRole::Viewfinder:
			pixelFormat = formats::NV12;
			pipe = &pipes_[C3ISPVIEW];
			break;

		case StreamRole::Raw:
			pixelFormat = data->bestRawFmt();
			if (!pixelFormat.isValid()) {
				LOG(C3ISP, Error)
					<< "Failed to get Raw format";
				return nullptr;
			}

			size = data->sensor_->resolution();
			pipe = &pipes_[C3ISPSTILL];
			break;

		default:
			LOG(C3ISP, Error) << "Invalid stream role: " << role;
			return nullptr;
		}

		std::map<PixelFormat, std::vector<SizeRange>> formats;
		for (const auto &c3Format : C3ISPFmtToCode) {
			PixelFormat pixFmt = c3Format.first;
			bool isRaw = isFmtRaw(pixFmt);

			if (pipe != &pipes_[C3ISPSTILL] && isRaw)
				continue;

			if (isRaw) {
				int rawCode = data->pixfmtToMbusCode(pixFmt);
				if (rawCode < 0)
					continue;

				const auto sizes = data->sensor_->sizes(rawCode);
				if (sizes.empty())
					continue;

				std::vector<SizeRange> sizeRanges;
				std::transform(sizes.begin(), sizes.end(),
					       std::back_inserter(sizeRanges),
					       [](const Size &s) {
						       return SizeRange(s);
					       });

				formats[pixFmt] = sizeRanges;
			} else {
				Size maxSize = std::min(kC3ISPMaxSize,
							data->sensor_->resolution());
				formats[pixFmt] = { kC3ISPMinSize, maxSize };
			}
		}

		StreamFormats streamFormats(formats);
		StreamConfiguration cfg(streamFormats);
		cfg.pixelFormat = pixelFormat;
		cfg.bufferCount = 4;
		cfg.size = size;

		config->addConfiguration(cfg);
	}

	if (config->validate() == CameraConfiguration::Invalid)
		return nullptr;

	return config;
}

int PipelineHandlerC3ISP::configureRawStream(C3ISPCameraData *data,
					     const StreamConfiguration &config,
					     V4L2SubdeviceFormat &subdevFormat)
{
	Stream *stream = config.stream();
	C3ISPPipe *pipe = pipeFromStream(data, stream);

	if (pipe != &pipes_[C3ISPSTILL]) {
		LOG(C3ISP, Error) << "Failed to match the RAW pipe";
		return -EINVAL;
	}

	const MediaEntity *resizerEntity = pipe->resizer->entity();
	int ret = resizerEntity->getPadByIndex(0)->links()[0]->setEnabled(true);
	if (ret) {
		LOG(C3ISP, Error) << "Couldn't enable resizer's sink link";
		return ret;
	}

	auto it = C3ISPFmtToCode.find(config.pixelFormat);
	if (it == C3ISPFmtToCode.end()) {
		LOG(C3ISP, Error) << "Failed to find the RAW pixel format";
		return -EINVAL;
	}

	subdevFormat.code = it->second;
	ret = isp_->setFormat(5, &subdevFormat);
	if (ret)
		return ret;

	ret = pipe->resizer->setFormat(0, &subdevFormat);
	if (ret)
		return ret;

	return 0;
}

int PipelineHandlerC3ISP::configureProcessedStream(C3ISPCameraData *data,
						   const StreamConfiguration &config,
						   V4L2SubdeviceFormat &subdevFormat)
{
	Stream *stream = config.stream();
	C3ISPPipe *pipe = pipeFromStream(data, stream);

	const MediaEntity *resizerEntity = pipe->resizer->entity();
	int ret = resizerEntity->getPadByIndex(0)->links()[0]->setEnabled(true);
	if (ret)
		return ret;

	auto it = C3ISPFmtToCode.find(config.pixelFormat);
	if (it == C3ISPFmtToCode.end()) {
		LOG(C3ISP, Error) << "Failed to find the processed pixel format";
		return -EINVAL;
	}

	unsigned int pad;
	if (pipe == &pipes_[C3ISPVIEW])
		pad = 3;
	else if (pipe == &pipes_[C3ISPVIDEO])
		pad = 4;
	else if (pipe == &pipes_[C3ISPSTILL])
		pad = 5;
	else {
		LOG(C3ISP, Error) << "Failed to match the pipe";
		return -EINVAL;
	}

	subdevFormat.code = it->second;
	ret = isp_->setFormat(pad, &subdevFormat);
	if (ret)
		return ret;

	ret = pipe->resizer->setFormat(0, &subdevFormat);
	if (ret)
		return ret;

	/* Compute the scaler-in to scaler-out ratio: first center-crop to align
	 * the FOV to the desired resolution, then scale to the desired size.
	 */
	Size scalerIn = subdevFormat.size.boundedToAspectRatio(config.size);
	int xCrop = (subdevFormat.size.width - scalerIn.width) / 2;
	int yCrop = (subdevFormat.size.height - scalerIn.height) / 2;

	Rectangle cropRect = { xCrop, yCrop, scalerIn };
	ret = pipe->resizer->setSelection(0, V4L2_SEL_TGT_CROP, &cropRect);
	if (ret)
		return ret;

	Rectangle scaleRect = { 0, 0, config.size };
	ret = pipe->resizer->setSelection(0, V4L2_SEL_TGT_COMPOSE, &scaleRect);
	if (ret)
		return ret;

	subdevFormat.size = scaleRect.size();

	return pipe->resizer->setFormat(1, &subdevFormat);
}

int PipelineHandlerC3ISP::configure(Camera *camera, CameraConfiguration *config)
{
	resetPipes();

	int ret = media_->disableLinks();
	if (ret)
		return ret;

	C3ISPCameraData *data = cameraData(camera);

	/* Enable the link between sensor and CSI-2 */
	const MediaEntity *csiEntity = data->csi_->entity();
	ret = csiEntity->getPadByIndex(0)->links()[0]->setEnabled(true);
	if (ret)
		return ret;

	/* Enable the link between CSI-2 and adapter */
	const MediaEntity *adapEntity = data->adap_->entity();
	ret = adapEntity->getPadByIndex(0)->links()[0]->setEnabled(true);
	if (ret)
		return ret;

	/* Enable the link between adapter and ISP */
	const MediaEntity *ispEntity = isp_->entity();
	ret = ispEntity->getPadByIndex(0)->links()[0]->setEnabled(true);
	if (ret)
		return ret;

	C3ISPCameraConfiguration *c3Config = static_cast<C3ISPCameraConfiguration *>(config);
	V4L2SubdeviceFormat subdevFormat = c3Config->sensorFormat_;

	ret = data->sensor_->setFormat(&subdevFormat, c3Config->combinedTransform());
	if (ret)
		return ret;

	ret = data->csi_->setFormat(0, &subdevFormat);
	if (ret)
		return ret;

	ret = data->adap_->setFormat(0, &subdevFormat);
	if (ret)
		return ret;

	ret = isp_->setFormat(0, &subdevFormat);
	if (ret)
		return ret;

	for (const StreamConfiguration &streamConfig : *config) {
		Stream *stream = streamConfig.stream();
		C3ISPPipe *pipe = pipeFromStream(data, stream);

		if (isFmtRaw(streamConfig.pixelFormat))
			ret = configureRawStream(data, streamConfig, subdevFormat);
		else
			ret = configureProcessedStream(data, streamConfig, subdevFormat);
		if (ret) {
			LOG(C3ISP, Error) << "Failed to configure pipeline";
			return ret;
		}

		V4L2DeviceFormat captureFormat;
		captureFormat.fourcc = pipe->cap->toV4L2PixelFormat(streamConfig.pixelFormat);
		captureFormat.size = streamConfig.size;

		ret = pipe->cap->setFormat(&captureFormat);
		if (ret)
			return ret;

		pipe->stream = stream;
	}

	/* Enable the link between parameter node and ISP */
	ret = ispEntity->getPadByIndex(1)->links()[0]->setEnabled(true);
	if (ret) {
		LOG(C3ISP, Error) << "Failed to enable param link";
		return ret;
	}

	/* Enable the link between ISP and 3A stats node */
	ret = ispEntity->getPadByIndex(2)->links()[0]->setEnabled(true);
	if (ret) {
		LOG(C3ISP, Error) << "Failed to enable stats link";
		return ret;
	}

	/* Inform the IPA of the sensor configuration */
	ipa::c3isp::IPAConfigInfo ipaConfig{};

	ret = data->sensor_->sensorInfo(&ipaConfig.sensorInfo);
	if (ret)
		return ret;

	ipaConfig.sensorControls = data->sensor_->controls();

	ControlInfoMap ipaControls;
	ret = data->ipa_->configure(ipaConfig, &ipaControls);
	if (ret) {
		LOG(C3ISP, Error) << "Failed to configure IPA";
		return ret;
	}

	data->updateControls(ipaControls);

	return 0;
}

int PipelineHandlerC3ISP::exportFrameBuffers(Camera *camera, Stream *stream,
					     std::vector<std::unique_ptr<FrameBuffer>> *buffers)
{
	C3ISPPipe *pipe = pipeFromStream(cameraData(camera), stream);
	unsigned int count = stream->configuration().bufferCount;

	return pipe->cap->exportBuffers(count, buffers);
}

int PipelineHandlerC3ISP::allocateBuffers(Camera *camera)
{
	C3ISPCameraData *data = cameraData(camera);
	unsigned int ipaBufferId = 1;
	unsigned int bufferCount;
	int ret;

	bufferCount = std::max({ data->viewStream.configuration().bufferCount,
				 data->videoStream.configuration().bufferCount,
				 data->stillStream.configuration().bufferCount });

	ret = stats_->allocateBuffers(bufferCount, &statsBuffers_);
	if (ret < 0)
		return ret;

	for (std::unique_ptr<FrameBuffer> &buffer : statsBuffers_) {
		buffer->setCookie(ipaBufferId++);
		data->ipaStatBuffers_.emplace_back(buffer->cookie(),
						   buffer->planes());
		availableStatsBuffers_.push(buffer.get());
	}

	ret = params_->allocateBuffers(bufferCount, &paramsBuffers_);
	if (ret < 0)
		return ret;

	for (std::unique_ptr<FrameBuffer> &buffer : paramsBuffers_) {
		buffer->setCookie(ipaBufferId++);
		data->ipaParamBuffers_.emplace_back(buffer->cookie(),
						    buffer->planes());
		availableParamsBuffers_.push(buffer.get());
	}

	data->ipa_->mapBuffers(data->ipaStatBuffers_, true);
	data->ipa_->mapBuffers(data->ipaParamBuffers_, false);

	return 0;
}

void PipelineHandlerC3ISP::freeBuffers(Camera *camera)
{
	C3ISPCameraData *data = cameraData(camera);

	while (!availableStatsBuffers_.empty())
		availableStatsBuffers_.pop();
	while (!availableParamsBuffers_.empty())
		availableParamsBuffers_.pop();

	statsBuffers_.clear();
	paramsBuffers_.clear();

	data->ipa_->unmapBuffers(data->ipaStatBuffers_);
	data->ipa_->unmapBuffers(data->ipaParamBuffers_);

	data->ipaStatBuffers_.clear();
	data->ipaParamBuffers_.clear();

	if (stats_->releaseBuffers())
		LOG(C3ISP, Error) << "Failed to release stats buffers";

	if (params_->releaseBuffers())
		LOG(C3ISP, Error) << "Failed to release params buffers";
}

int PipelineHandlerC3ISP::pipesStart()
{
	for (C3ISPPipe &pipe : pipes_) {
		if (!pipe.stream)
			continue;

		Stream *stream = pipe.stream;

		int ret = pipe.cap->importBuffers(stream->configuration().bufferCount);
		if (ret) {
			LOG(C3ISP, Error) << "Failed to import buffers";
			return ret;
		}

		ret = pipe.cap->streamOn();
		if (ret) {
			LOG(C3ISP, Error) << "Failed to start stream";
			return ret;
		}
	}

	return 0;
}

void PipelineHandlerC3ISP::pipesStop()
{
	for (C3ISPPipe &pipe : pipes_) {
		if (!pipe.stream)
			continue;

		pipe.cap->streamOff();
		pipe.cap->releaseBuffers();
	}
}

int PipelineHandlerC3ISP::start(Camera *camera,
				[[maybe_unused]] const ControlList *controls)
{
	C3ISPCameraData *data = cameraData(camera);
	int ret;

	ret = allocateBuffers(camera);
	if (ret)
		return ret;

	ret = data->ipa_->start();
	if (ret) {
		LOG(C3ISP, Error) << "Failed to start IPA";

		freeBuffers(camera);
		return ret;
	}

	ret = pipesStart();
	if (ret) {
		LOG(C3ISP, Error) << "Failed to start pipe";

		data->ipa_->stop();
		freeBuffers(camera);
		return ret;
	}

	ret = stats_->streamOn();
	if (ret) {
		LOG(C3ISP, Error) << "Failed to start stats";

		pipesStop();
		data->ipa_->stop();
		freeBuffers(camera);
		return ret;
	}

	ret = params_->streamOn();
	if (ret) {
		LOG(C3ISP, Error) << "Failed to start params";

		stats_->streamOff();
		pipesStop();
		data->ipa_->stop();
		freeBuffers(camera);
		return ret;
	}

	ret = isp_->setFrameStartEnabled(true);
	if (ret) {
		LOG(C3ISP, Error) << "Failed to enable frame start";

		params_->streamOff();
		stats_->streamOff();
		pipesStop();
		data->ipa_->stop();
		freeBuffers(camera);
		return ret;
	}

	return 0;
}

void PipelineHandlerC3ISP::stopDevice(Camera *camera)
{
	C3ISPCameraData *data = cameraData(camera);

	isp_->setFrameStartEnabled(false);

	params_->streamOff();
	stats_->streamOff();
	data->ipa_->stop();
	freeBuffers(camera);

	pipesStop();
	clearIncompleteRequests();
}

int PipelineHandlerC3ISP::queueRequestDevice(Camera *camera, Request *request)
{
	C3ISPCameraData *data = cameraData(camera);

	if (availableStatsBuffers_.empty()) {
		LOG(C3ISP, Error) << "No available stats buffer";
		return -ENOENT;
	}

	if (availableParamsBuffers_.empty()) {
		LOG(C3ISP, Error) << "No available params buffer";
		return -ENOENT;
	}

	C3ISPFrameInfo frameInfo;
	frameInfo.request = request;

	frameInfo.statBuffer = availableStatsBuffers_.front();
	availableStatsBuffers_.pop();
	frameInfo.paramBuffer = availableParamsBuffers_.front();
	availableParamsBuffers_.pop();

	frameInfo.paramsDone = false;
	frameInfo.statsDone = false;

	frameInfoMap_[request->sequence()] = frameInfo;

	data->ipa_->queueRequest(request->sequence(), request->controls());
	data->ipa_->computeParams(request->sequence(),
				  frameInfo.paramBuffer->cookie());

	return 0;
}

C3ISPFrameInfo *PipelineHandlerC3ISP::findFrameInfo(Request *request)
{
	for (auto &[sequence, info] : frameInfoMap_) {
		if (info.request == request)
			return &info;
	}

	return nullptr;
}

C3ISPFrameInfo *PipelineHandlerC3ISP::findFrameInfo(FrameBuffer *buffer)
{
	for (auto &[sequence, info] : frameInfoMap_) {
		if (info.paramBuffer == buffer || info.statBuffer == buffer)
			return &info;
	}

	return nullptr;
}

void PipelineHandlerC3ISP::clearIncompleteRequests()
{
	for (auto &[sequence, info] : frameInfoMap_) {
		if (info.request)
			cancelRequest(info.request);
	}

	frameInfoMap_.clear();
}

void PipelineHandlerC3ISP::tryComplete(C3ISPFrameInfo *info)
{
	if (!info->paramsDone)
		return;

	if (!info->statsDone)
		return;

	Request *request = info->request;
	if (request->hasPendingBuffers())
		return;

	if (info->statBuffer)
		availableStatsBuffers_.push(info->statBuffer);

	if (info->paramBuffer)
		availableParamsBuffers_.push(info->paramBuffer);

	frameInfoMap_.erase(request->sequence());

	completeRequest(request);
}

void PipelineHandlerC3ISP::imageBufferReady(FrameBuffer *buffer)
{
	Request *request = buffer->request();
	C3ISPFrameInfo *frameInfo = findFrameInfo(request);
	ASSERT(frameInfo);

	if (completeBuffer(request, buffer))
		tryComplete(frameInfo);
}

void PipelineHandlerC3ISP::paramsBufferReady(FrameBuffer *buffer)
{
	C3ISPFrameInfo *frameInfo = findFrameInfo(buffer);
	ASSERT(frameInfo);

	frameInfo->paramsDone = true;

	tryComplete(frameInfo);
}

void PipelineHandlerC3ISP::statsBufferReady(FrameBuffer *buffer)
{
	C3ISPFrameInfo *frameInfo = findFrameInfo(buffer);
	ASSERT(frameInfo);

	Request *request = frameInfo->request;
	C3ISPCameraData *data = cameraData(request->_d()->camera());

	ControlList sensorControls = data->delayedCtrls_->get(buffer->metadata().sequence);

	data->ipa_->processStats(request->sequence(), buffer->cookie(),
				 sensorControls);
}

void PipelineHandlerC3ISP::paramsComputed(unsigned int requestId,
					  unsigned int bytesused)
{
	C3ISPFrameInfo &frameInfo = frameInfoMap_[requestId];
	Request *request = frameInfo.request;
	C3ISPCameraData *data = cameraData(request->_d()->camera());

	frameInfo.paramBuffer->_d()->metadata().planes()[0].bytesused = bytesused;

	params_->queueBuffer(frameInfo.paramBuffer);
	stats_->queueBuffer(frameInfo.statBuffer);

	for (auto &[stream, buffer] : request->buffers()) {
		C3ISPPipe *pipe = pipeFromStream(data, stream);

		pipe->cap->queueBuffer(buffer);
	}
}

void PipelineHandlerC3ISP::statsProcessed(unsigned int requestId,
					  const ControlList &metadata)
{
	C3ISPFrameInfo &frameInfo = frameInfoMap_[requestId];

	frameInfo.statsDone = true;
	frameInfo.request->metadata().merge(metadata);

	tryComplete(&frameInfo);
}

bool PipelineHandlerC3ISP::createCamera(MediaLink *ispLink)
{
	MediaEntity *adap = ispLink->source()->entity();
	const MediaPad *adapSink = adap->getPadByIndex(0);
	MediaEntity *csi = adapSink->links()[0]->source()->entity();
	const MediaPad *csiSink = csi->getPadByIndex(0);

	MediaEntity *sensor = csiSink->links()[0]->source()->entity();
	if (sensor->function() != MEDIA_ENT_F_CAM_SENSOR)
		return false;

	std::unique_ptr<C3ISPCameraData> data =
		std::make_unique<C3ISPCameraData>(this, sensor);
	if (data->init())
		return false;

	/* Generic values for sensor */
	const CameraSensorProperties::SensorDelays &delays = data->sensor_->sensorDelays();
	std::unordered_map<uint32_t, DelayedControls::ControlParams> params = {
		{ V4L2_CID_ANALOGUE_GAIN, { delays.gainDelay, false } },
		{ V4L2_CID_EXPOSURE, { delays.exposureDelay, false } },
	};

	data->delayedCtrls_ =
		std::make_unique<DelayedControls>(data->sensor_->device(), params);
	isp_->frameStart.connect(data->delayedCtrls_.get(),
				 &DelayedControls::applyControls);

	if (data->loadIPA())
		return false;

	data->ipa_->statsProcessed.connect(this, &PipelineHandlerC3ISP::statsProcessed);
	data->ipa_->paramsComputed.connect(this, &PipelineHandlerC3ISP::paramsComputed);

	std::set<Stream *> streams{ &data->viewStream, &data->stillStream, &data->videoStream };

	std::shared_ptr<Camera> camera = Camera::create(std::move(data), sensor->name(), streams);

	registerCamera(std::move(camera));

	return true;
}

bool PipelineHandlerC3ISP::match(DeviceEnumerator *enumerator)
{
	const MediaPad *ispSink;

	DeviceMatch dm("c3-isp");
	dm.add("c3-mipi-csi2");
	dm.add("c3-mipi-adapter");
	dm.add("c3-isp-core");

	media_ = acquireMediaDevice(enumerator, dm);
	if (!media_)
		return false;

	isp_ = V4L2Subdevice::fromEntityName(media_, "c3-isp-core");
	if (isp_->open() < 0)
		return false;

	stats_ = V4L2VideoDevice::fromEntityName(media_, "c3-isp-stats");
	if (stats_->open() < 0)
		return false;

	params_ = V4L2VideoDevice::fromEntityName(media_, "c3-isp-params");
	if (params_->open() < 0)
		return false;

	for (unsigned int i = C3ISPVIEW; i < C3ISPNumPipes; i++) {
		C3ISPPipe *ispPipe = &pipes_[i];

		std::ostringstream resizer;
		resizer << "c3-isp-resizer" << i;
		ispPipe->resizer = V4L2Subdevice::fromEntityName(media_, resizer.str());
		if (ispPipe->resizer->open() < 0)
			return false;

		std::ostringstream capture;
		capture << "c3-isp-cap" << i;
		ispPipe->cap = V4L2VideoDevice::fromEntityName(media_, capture.str());
		if (ispPipe->cap->open() < 0)
			return false;

		ispPipe->cap->bufferReady.connect(this, &PipelineHandlerC3ISP::imageBufferReady);
	}

	stats_->bufferReady.connect(this, &PipelineHandlerC3ISP::statsBufferReady);
	params_->bufferReady.connect(this, &PipelineHandlerC3ISP::paramsBufferReady);

	ispSink = isp_->entity()->getPadByIndex(0);
	if (!ispSink || ispSink->links().empty())
		return false;

	if (!createCamera(ispSink->links()[0])) {
		LOG(C3ISP, Error) << "Failed to create camera";
		return false;
	}

	return true;
}

REGISTER_PIPELINE_HANDLER(PipelineHandlerC3ISP, "c3isp")

} /* namespace libcamera */
