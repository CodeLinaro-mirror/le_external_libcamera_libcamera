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

#include <libcamera/ipa/c3isp_ipa_interface.h>
#include <libcamera/ipa/c3isp_ipa_proxy.h>

#include "libcamera/internal/bayer_format.h"
#include "libcamera/internal/camera.h"
#include "libcamera/internal/camera_sensor.h"
#include "libcamera/internal/delayed_controls.h"
#include "libcamera/internal/device_enumerator.h"
#include "libcamera/internal/framebuffer.h"
#include "libcamera/internal/ipa_manager.h"
#include "libcamera/internal/media_device.h"
#include "libcamera/internal/pipeline_handler.h"
#include "libcamera/internal/v4l2_subdevice.h"
#include "libcamera/internal/v4l2_videodevice.h"

namespace libcamera {

LOG_DEFINE_CATEGORY(C3ISP)

class PipelineHandlerC3ISP;
class C3ISPCameraData;

const std::map<libcamera::PixelFormat, unsigned int> C3ISPFmtToCode = {
	{ formats::R8, MEDIA_BUS_FMT_YUV8_1X24 },
	{ formats::NV12, MEDIA_BUS_FMT_YUV8_1X24 },
	{ formats::NV21, MEDIA_BUS_FMT_YUV8_1X24 },
	{ formats::NV16, MEDIA_BUS_FMT_YUV8_1X24 },
	{ formats::NV61, MEDIA_BUS_FMT_YUV8_1X24 },
};

constexpr Size kC3ISPMinSize = { 160, 120 };
constexpr Size kC3ISPMaxSize = { 2888, 2240 };

struct C3ISPFrameInfo {
	unsigned int frame;
	Request *request;

	FrameBuffer *paramBuffer;
	FrameBuffer *statBuffer;
	FrameBuffer *viewBuffer;
	FrameBuffer *stillBuffer;
	FrameBuffer *videoBuffer;

	bool paramDequeued;
	bool metadataProcessed;
};

class C3ISPFrames
{
public:
	C3ISPFrames(PipelineHandler *pipe);

	C3ISPFrameInfo *create(const C3ISPCameraData *data, Request *request);
	int destroy(unsigned int frame);
	void clear();

	C3ISPFrameInfo *find(unsigned int frame);
	C3ISPFrameInfo *find(FrameBuffer *buffer);
	C3ISPFrameInfo *find(Request *request);

private:
	PipelineHandlerC3ISP *pipe_;
	std::map<unsigned int, C3ISPFrameInfo *> frameInfo_;
};

class C3ISPCameraData : public Camera::Private
{
public:
	C3ISPCameraData(PipelineHandler *pipe, MediaEntity *entity)
		: Camera::Private(pipe), entity_(entity), frame_(0), frameInfo_(pipe)
	{
	}

	int init();
	PipelineHandlerC3ISP *pipe();
	int loadIPA(unsigned int hwRevision);

	MediaEntity *entity_;
	std::unique_ptr<CameraSensor> sensor_;
	std::unique_ptr<DelayedControls> delayedCtrls_;
	std::unique_ptr<V4L2Subdevice> csi_;
	std::unique_ptr<V4L2Subdevice> adap_;
	Stream viewStream;
	Stream stillStream;
	Stream videoStream;
	unsigned int frame_;
	std::vector<IPABuffer> ipaBuffers_;
	C3ISPFrames frameInfo_;

	std::unique_ptr<ipa::c3isp::IPAProxyC3ISP> ipa_;

private:
	void paramFilled(unsigned int frame, unsigned int bytesused);
	void setSensorControls(unsigned int frame,
			       const ControlList &sensorControls);
	void metadataReady(unsigned int frame, const ControlList &metadata);
};

class C3ISPCameraConfiguration : public CameraConfiguration
{
public:
	C3ISPCameraConfiguration(C3ISPCameraData *data)
		: CameraConfiguration(), data_(data)
	{
	}

	Status validate() override;

	V4L2SubdeviceFormat sensorFormat_;

private:
	static constexpr unsigned int kMaxStreams = 3;

	const C3ISPCameraData *data_;
};

class PipelineHandlerC3ISP : public PipelineHandler
{
public:
	PipelineHandlerC3ISP(CameraManager *manager);

	std::unique_ptr<CameraConfiguration> generateConfiguration(Camera *camera,
								   Span<const StreamRole> roles) override;
	int configure(Camera *camera, CameraConfiguration *config) override;

	int exportFrameBuffers(Camera *camera, Stream *stream,
			       std::vector<std::unique_ptr<FrameBuffer>> *buffers) override;

	int start(Camera *camera, const ControlList *controls) override;
	void stopDevice(Camera *camera) override;

	int queueRequestDevice(Camera *camera, Request *request) override;

	void bufferReady(FrameBuffer *buffer);
	void statReady(FrameBuffer *buffer);
	void paramReady(FrameBuffer *buffer);

	bool match(DeviceEnumerator *enumerator) override;

private:
	friend C3ISPCameraData;
	friend C3ISPFrames;

	struct C3ISPPipe {
		std::unique_ptr<V4L2Subdevice> resizer;
		std::unique_ptr<V4L2VideoDevice> cap;
		Stream *stream;
	};

	enum {
		C3ISPVIEW,
		C3ISPSTILL,
		C3ISPVIDEO,
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

	int pipeStart();
	void pipeStop();

	int setConfigStreams(CameraConfiguration *config);
	int configureProcessedStream(C3ISPCameraData *data,
				     const StreamConfiguration &config,
				     V4L2SubdeviceFormat &subdevFormat);

	bool createCamera(MediaLink *ispLink);
	void tryCompleteRequest(C3ISPFrameInfo *info);
	int allocateBuffers(Camera *camera);
	int freeBuffers(Camera *camera);

	MediaDevice *media_;
	std::unique_ptr<V4L2Subdevice> isp_;
	std::unique_ptr<V4L2VideoDevice> param_;
	std::unique_ptr<V4L2VideoDevice> stat_;

	std::vector<std::unique_ptr<FrameBuffer>> paramBuffers_;
	std::vector<std::unique_ptr<FrameBuffer>> statBuffers_;
	std::queue<FrameBuffer *> availableParamBuffers_;
	std::queue<FrameBuffer *> availableStatBuffers_;

	std::vector<Stream *> streams_;
	std::array<C3ISPPipe, C3ISPNumPipes> pipes_;

	Camera *activeCamera_;
};

C3ISPFrames::C3ISPFrames(PipelineHandler *pipe)
	: pipe_(static_cast<PipelineHandlerC3ISP *>(pipe))
{
}

C3ISPFrameInfo *C3ISPFrames::create(const C3ISPCameraData *data, Request *request)
{
	unsigned int frame = data->frame_;

	FrameBuffer *paramBuffer = nullptr;
	FrameBuffer *statBuffer = nullptr;

	if (pipe_->availableParamBuffers_.empty()) {
		LOG(C3ISP, Error) << "Param buffer queue empty";
		return nullptr;
	}

	if (pipe_->availableStatBuffers_.empty()) {
		LOG(C3ISP, Error) << "Stat buffer queue empty";
		return nullptr;
	}

	paramBuffer = pipe_->availableParamBuffers_.front();
	pipe_->availableParamBuffers_.pop();

	statBuffer = pipe_->availableStatBuffers_.front();
	pipe_->availableStatBuffers_.pop();

	FrameBuffer *viewBuffer = request->findBuffer(&data->viewStream);
	FrameBuffer *stillBuffer = request->findBuffer(&data->stillStream);
	FrameBuffer *videoBuffer = request->findBuffer(&data->videoStream);

	C3ISPFrameInfo *info = new C3ISPFrameInfo;

	info->frame = frame;
	info->request = request;
	info->paramBuffer = paramBuffer;
	info->statBuffer = statBuffer;
	info->viewBuffer = viewBuffer;
	info->stillBuffer = stillBuffer;
	info->videoBuffer = videoBuffer;
	info->paramDequeued = false;
	info->metadataProcessed = false;

	frameInfo_[frame] = info;

	return info;
}

int C3ISPFrames::destroy(unsigned int frame)
{
	C3ISPFrameInfo *info = find(frame);
	if (!info)
		return -ENOENT;

	pipe_->availableParamBuffers_.push(info->paramBuffer);
	pipe_->availableStatBuffers_.push(info->statBuffer);

	frameInfo_.erase(info->frame);

	delete info;

	return 0;
}

void C3ISPFrames::clear()
{
	for (const auto &entry : frameInfo_) {
		C3ISPFrameInfo *info = entry.second;

		pipe_->availableParamBuffers_.push(info->paramBuffer);
		pipe_->availableStatBuffers_.push(info->statBuffer);

		delete info;
	}

	frameInfo_.clear();
}

C3ISPFrameInfo *C3ISPFrames::find(unsigned int frame)
{
	auto itInfo = frameInfo_.find(frame);

	if (itInfo != frameInfo_.end())
		return itInfo->second;

	LOG(C3ISP, Fatal) << "Can't locate info from frame";

	return nullptr;
}

C3ISPFrameInfo *C3ISPFrames::find(FrameBuffer *buffer)
{
	for (auto &itInfo : frameInfo_) {
		C3ISPFrameInfo *info = itInfo.second;

		if (info->paramBuffer == buffer ||
		    info->statBuffer == buffer ||
		    info->viewBuffer == buffer ||
		    info->stillBuffer == buffer ||
		    info->videoBuffer == buffer)
			return info;
	}

	LOG(C3ISP, Fatal) << "Can't locate info from buffer";

	return nullptr;
}

C3ISPFrameInfo *C3ISPFrames::find(Request *request)
{
	for (auto &itInfo : frameInfo_) {
		C3ISPFrameInfo *info = itInfo.second;

		if (info->request == request)
			return info;
	}

	LOG(C3ISP, Fatal) << "Can't locate info from request";

	return nullptr;
}

int C3ISPCameraData::init()
{
	int ret;

	/* Register a CameraSensor */
	sensor_ = CameraSensorFactoryBase::create(entity_);
	if (!sensor_)
		return ret;

	const MediaPad *sensorSrc = entity_->getPadByIndex(0);
	MediaEntity *csiEntity = sensorSrc->links()[0]->sink()->entity();

	csi_ = std::make_unique<V4L2Subdevice>(csiEntity);
	if (csi_->open()) {
		LOG(C3ISP, Error) << "Failed to open CSI-2 subdevice";
		return false;
	}

	const MediaPad *csiSrc = csiEntity->getPadByIndex(1);
	MediaEntity *adapEntity = csiSrc->links()[0]->sink()->entity();

	adap_ = std::make_unique<V4L2Subdevice>(adapEntity);
	if (adap_->open()) {
		LOG(C3ISP, Error) << "Failed to open adapter subdevice";
		return false;
	}

	return 0;
}

PipelineHandlerC3ISP *C3ISPCameraData::pipe()
{
	return static_cast<PipelineHandlerC3ISP *>(Camera::Private::pipe());
}

int C3ISPCameraData::loadIPA(unsigned int hwRevision)
{
	ipa_ = IPAManager::createIPA<ipa::c3isp::IPAProxyC3ISP>(pipe(), 1, 1);
	if (!ipa_)
		return -ENOENT;

	ipa_->setSensorControls.connect(this, &C3ISPCameraData::setSensorControls);
	ipa_->paramsBufferReady.connect(this, &C3ISPCameraData::paramFilled);
	ipa_->metadataReady.connect(this, &C3ISPCameraData::metadataReady);

	/*
	 * The API tuning file is made from the sensor name unless the
	 * environment variable overrides it.
	 */
	std::string ipaTuningFile;
	char const *configFromEnv = utils::secure_getenv("LIBCAMERA_C3ISP_TUNING_FILE");
	if (!configFromEnv || *configFromEnv == '\0') {
		ipaTuningFile = ipa_->configurationFile(sensor_->model() + ".yaml");
		if (ipaTuningFile.empty())
			ipaTuningFile = ipa_->configurationFile("uncalibrated.yaml");
	} else {
		ipaTuningFile = std::string(configFromEnv);
	}

	IPACameraSensorInfo sensorInfo{};
	int ret = sensor_->sensorInfo(&sensorInfo);
	if (ret) {
		LOG(C3ISP, Error) << "Invalid semsor information";
		return ret;
	}

	ret = ipa_->init({ ipaTuningFile, sensor_->model() }, hwRevision,
			 sensorInfo, sensor_->controls(), &controlInfo_);
	if (ret) {
		LOG(C3ISP, Error) << "Failed to initialize IPA";
		return ret;
	}

	return 0;
}

void C3ISPCameraData::paramFilled(unsigned int frame, unsigned int bytesused)
{
	PipelineHandlerC3ISP *pipe = C3ISPCameraData::pipe();
	C3ISPFrameInfo *info = frameInfo_.find(frame);
	if (!info)
		return;

	info->paramBuffer->_d()->metadata().planes()[0].bytesused = bytesused;
	pipe->param_->queueBuffer(info->paramBuffer);
	pipe->stat_->queueBuffer(info->statBuffer);

	if (info->viewBuffer)
		pipe->pipes_[PipelineHandlerC3ISP::C3ISPVIEW].cap->queueBuffer(info->viewBuffer);

	if (info->stillBuffer)
		pipe->pipes_[PipelineHandlerC3ISP::C3ISPSTILL].cap->queueBuffer(info->stillBuffer);

	if (info->videoBuffer)
		pipe->pipes_[PipelineHandlerC3ISP::C3ISPVIDEO].cap->queueBuffer(info->videoBuffer);
}

void C3ISPCameraData::setSensorControls([[maybe_unused]] unsigned int frame,
					const ControlList &sensorControls)
{
	delayedCtrls_->push(sensorControls);
}

void C3ISPCameraData::metadataReady(unsigned int frame, const ControlList &metadata)
{
	C3ISPFrameInfo *info = frameInfo_.find(frame);
	if (!info)
		return;

	info->request->metadata().merge(metadata);
	info->metadataProcessed = true;

	pipe()->tryCompleteRequest(info);
}

namespace {

const std::map<PixelFormat, uint32_t> rawFormats = {
	{ formats::SBGGR10, MEDIA_BUS_FMT_SBGGR10_1X10 },
	{ formats::SGBRG10, MEDIA_BUS_FMT_SGBRG10_1X10 },
	{ formats::SGRBG10, MEDIA_BUS_FMT_SGRBG10_1X10 },
	{ formats::SRGGB10, MEDIA_BUS_FMT_SRGGB10_1X10 },
	{ formats::SBGGR12, MEDIA_BUS_FMT_SBGGR12_1X12 },
	{ formats::SGBRG12, MEDIA_BUS_FMT_SGBRG12_1X12 },
	{ formats::SGRBG12, MEDIA_BUS_FMT_SGRBG12_1X12 },
	{ formats::SRGGB12, MEDIA_BUS_FMT_SRGGB12_1X12 },
};

};

CameraConfiguration::Status C3ISPCameraConfiguration::validate()
{
	const CameraSensor *sensor = data_->sensor_.get();
	Status status = Valid;

	if (config_.empty())
		return Invalid;

	if (config_.size() > kMaxStreams) {
		config_.resize(kMaxStreams);
		status = Adjusted;
	}

	Size maxSize;

	for (StreamConfiguration &config : config_) {
		const auto it = C3ISPFmtToCode.find(config.pixelFormat);
		if (it == C3ISPFmtToCode.end()) {
			LOG(C3ISP, Debug)
				<< "Format adjusted to " << formats::NV12;
			config.pixelFormat = formats::NV12;
			status = Adjusted;
		}

		Size size = std::clamp(config.size, kC3ISPMinSize, kC3ISPMaxSize);
		if (size != config.size) {
			LOG(C3ISP, Debug)
				<< "Size adjusted to " << size;
			config.size = size;
			status = Adjusted;
		}

		maxSize = std::max(maxSize, config.size);
	}

	std::vector<unsigned int> mbusCodes;

	std::transform(rawFormats.begin(), rawFormats.end(),
		       std::back_inserter(mbusCodes),
		       [](const auto &value) { return value.second; });

	sensorFormat_ = sensor->getFormat(mbusCodes, maxSize);

	if (sensorFormat_.size.isNull())
		sensorFormat_.size = sensor->resolution();

	return status;
}

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

	if (roles.empty())
		return config;

	streams_.clear();

	for (const StreamRole &role : roles) {
		PixelFormat pixelFormat;
		Size size = std::min(Size{ 1920, 1080 }, data->sensor_->resolution());

		switch (role) {
		case StreamRole::StillCapture:
			pixelFormat = formats::NV12;
			streams_.push_back(&data->stillStream);
			break;

		case StreamRole::VideoRecording:
			pixelFormat = formats::NV12;
			streams_.push_back(&data->videoStream);
			break;

		case StreamRole::Viewfinder:
			pixelFormat = formats::NV12;
			streams_.push_back(&data->viewStream);
			break;

		default:
			LOG(C3ISP, Error) << "Invalid stream role: " << role;
			return nullptr;
		}

		std::map<PixelFormat, std::vector<SizeRange>> formats;
		for (const auto &c3Format : C3ISPFmtToCode) {
			PixelFormat pixFmt = c3Format.first;
			Size maxSize = std::min(kC3ISPMaxSize, data->sensor_->resolution());
			formats[pixFmt] = { kC3ISPMinSize, maxSize };
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

int PipelineHandlerC3ISP::configureProcessedStream(C3ISPCameraData *data,
						   const StreamConfiguration &config,
						   V4L2SubdeviceFormat &subdevFormat)
{
	Stream *stream = config.stream();
	C3ISPPipe *pipe = pipeFromStream(data, stream);
	V4L2SubdeviceFormat rszFormat;

	const MediaEntity *resizerEntity = pipe->resizer->entity();
	int ret = resizerEntity->getPadByIndex(0)->links()[0]->setEnabled(true);
	if (ret)
		return ret;

	ret = resizerEntity->getPadByIndex(1)->links()[0]->setEnabled(true);
	if (ret)
		return ret;

	rszFormat.code = C3ISPFmtToCode.find(config.pixelFormat)->second;
	rszFormat.size = subdevFormat.size;
	rszFormat.colorSpace = subdevFormat.colorSpace;

	ret = pipe->resizer->setFormat(0, &rszFormat);
	if (ret)
		return ret;

	Rectangle cropRect = { 0, 0, rszFormat.size };
	ret = pipe->resizer->setSelection(0, V4L2_SEL_TGT_CROP, &cropRect);
	if (ret)
		return ret;

	Rectangle scaleRect = { 0, 0, config.size };
	ret = pipe->resizer->setSelection(0, V4L2_SEL_TGT_COMPOSE, &scaleRect);
	if (ret)
		return ret;

	return 0;
}

int PipelineHandlerC3ISP::setConfigStreams(CameraConfiguration *config)
{
	if (config->size() != streams_.size()) {
		LOG(C3ISP, Error) << "Invalid configuration size: " << config->size();
		return -EINVAL;
	}

	for (unsigned int i = 0; i < config->size(); i++)
		config->at(i).setStream(streams_[i]);

	return 0;
}

int PipelineHandlerC3ISP::configure(Camera *camera, CameraConfiguration *config)
{
	resetPipes();

	int ret = media_->disableLinks();
	if (ret)
		return ret;

	/*
	 * The stream has been set to nullptr in Camera::configure,
	 * so need to set stream.
	 */
	ret = setConfigStreams(config);
	if (ret)
		return ret;

	/* Link the graph */
	C3ISPCameraData *data = cameraData(camera);

	const MediaEntity *csiEntity = data->csi_->entity();
	ret = csiEntity->getPadByIndex(0)->links()[0]->setEnabled(true);
	if (ret)
		return ret;

	const MediaEntity *adapEntity = data->adap_->entity();
	ret = adapEntity->getPadByIndex(0)->links()[0]->setEnabled(true);
	if (ret)
		return ret;

	const MediaEntity *ispEntity = isp_->entity();
	ret = ispEntity->getPadByIndex(0)->links()[0]->setEnabled(true);
	if (ret)
		return ret;

	ret = ispEntity->getPadByIndex(1)->links()[0]->setEnabled(true);
	if (ret)
		return ret;

	ret = ispEntity->getPadByIndex(2)->links()[0]->setEnabled(true);
	if (ret)
		return ret;

	C3ISPCameraConfiguration *c3Config = static_cast<C3ISPCameraConfiguration *>(config);
	V4L2SubdeviceFormat subdevFormat = c3Config->sensorFormat_;

	ret = data->sensor_->setFormat(&subdevFormat);
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

	V4L2SubdeviceFormat ispSrcVideoFormat = subdevFormat;
	ispSrcVideoFormat.code = MEDIA_BUS_FMT_YUV8_1X24;
	ret = isp_->setFormat(3, &ispSrcVideoFormat);
	if (ret)
		return ret;

	V4L2DeviceFormat paramFormat;
	paramFormat.fourcc = V4L2PixelFormat(V4L2_META_FMT_C3ISP_PARAMS);
	ret = param_->setFormat(&paramFormat);
	if (ret)
		return ret;

	V4L2DeviceFormat statFormat;
	statFormat.fourcc = V4L2PixelFormat(V4L2_META_FMT_C3ISP_STATS);
	ret = stat_->setFormat(&statFormat);
	if (ret)
		return ret;

	for (const StreamConfiguration &streamConfig : *config) {
		Stream *stream = streamConfig.stream();
		C3ISPPipe *pipe = pipeFromStream(data, stream);

		ret = configureProcessedStream(data, streamConfig, subdevFormat);
		if (ret) {
			LOG(C3ISP, Error) << "Failed to configure process stream";
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

	/* Configure IPA module */
	ipa::c3isp::IPAConfigInfo ipaConfig{};

	ret = data->sensor_->sensorInfo(&ipaConfig.sensorInfo);
	if (ret)
		return ret;

	ipaConfig.sensorControls = data->sensor_->controls();

	ret = data->ipa_->configure(ipaConfig, &data->controlInfo_);
	if (ret) {
		LOG(C3ISP, Error) << "Failed to configure IPA";
		return ret;
	}

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

	int ret = param_->allocateBuffers(4, &paramBuffers_);
	if (ret < 0)
		return ret;

	ret = stat_->allocateBuffers(4, &statBuffers_);
	if (ret < 0) {
		paramBuffers_.clear();
		statBuffers_.clear();
		return ret;
	}

	for (std::unique_ptr<FrameBuffer> &buffer : paramBuffers_) {
		buffer->setCookie(ipaBufferId++);
		data->ipaBuffers_.emplace_back(buffer->cookie(), buffer->planes());
		availableParamBuffers_.push(buffer.get());
	}

	for (std::unique_ptr<FrameBuffer> &buffer : statBuffers_) {
		buffer->setCookie(ipaBufferId++);
		data->ipaBuffers_.emplace_back(buffer->cookie(), buffer->planes());
		availableStatBuffers_.push(buffer.get());
	}

	data->ipa_->mapBuffers(data->ipaBuffers_);

	return 0;
}

int PipelineHandlerC3ISP::freeBuffers(Camera *camera)
{
	C3ISPCameraData *data = cameraData(camera);

	while (!availableStatBuffers_.empty())
		availableStatBuffers_.pop();

	while (!availableParamBuffers_.empty())
		availableParamBuffers_.pop();

	paramBuffers_.clear();
	statBuffers_.clear();

	std::vector<unsigned int> ids;
	for (IPABuffer &ipabuf : data->ipaBuffers_)
		ids.push_back(ipabuf.id);

	data->ipa_->unmapBuffers(ids);
	data->ipaBuffers_.clear();

	if (param_->releaseBuffers())
		LOG(C3ISP, Error) << "Failed to release param buffers";

	if (stat_->releaseBuffers())
		LOG(C3ISP, Error) << "Failed to release stat buffers";

	return 0;
}

int PipelineHandlerC3ISP::pipeStart()
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

void PipelineHandlerC3ISP::pipeStop()
{
	for (C3ISPPipe &pipe : pipes_) {
		if (!pipe.stream)
			continue;

		pipe.cap->streamOff();
		pipe.cap->releaseBuffers();
	}
}

int PipelineHandlerC3ISP::start([[maybe_unused]] Camera *camera,
				[[maybe_unused]] const ControlList *controls)
{
	C3ISPCameraData *data = cameraData(camera);
	int ret;

	ret = allocateBuffers(camera);
	if (ret < 0)
		return ret;

	ret = data->ipa_->start();
	if (ret) {
		LOG(C3ISP, Error) << "Failed to start IPA";
		goto error;
	}

	data->frame_ = 0;

	ret = param_->streamOn();
	if (ret) {
		LOG(C3ISP, Error) << "Failed to start param";
		goto error;
	}

	ret = stat_->streamOn();
	if (ret) {
		LOG(C3ISP, Error) << "Failed to start stat";
		goto error;
	}

	ret = pipeStart();
	if (ret) {
		LOG(C3ISP, Error) << "Failed to start pipe";
		goto error;
	}

	ret = isp_->setFrameStartEnabled(true);
	if (ret) {
		LOG(C3ISP, Error) << "Failed to set frame start";
		goto error;
	}

	activeCamera_ = camera;

	return 0;

error:
	pipeStop();
	stat_->streamOff();
	param_->streamOff();
	data->ipa_->stop();
	freeBuffers(camera);
	LOG(C3ISP, Error) << "Failed to start camera " << camera->id();

	return ret;
}

void PipelineHandlerC3ISP::stopDevice([[maybe_unused]] Camera *camera)
{
	C3ISPCameraData *data = cameraData(camera);

	isp_->setFrameStartEnabled(false);

	data->ipa_->stop();

	pipeStop();

	stat_->streamOff();
	param_->streamOff();

	data->frameInfo_.clear();

	freeBuffers(camera);

	activeCamera_ = nullptr;
}

int PipelineHandlerC3ISP::queueRequestDevice(Camera *camera, Request *request)
{
	C3ISPCameraData *data = cameraData(camera);

	C3ISPFrameInfo *info = data->frameInfo_.create(data, request);
	if (!info)
		return -ENOENT;

	data->ipa_->queueRequest(data->frame_, request->controls());

	data->ipa_->fillParamsBuffer(data->frame_, info->paramBuffer->cookie());

	data->frame_++;

	return 0;
}

void PipelineHandlerC3ISP::tryCompleteRequest(C3ISPFrameInfo *info)
{
	C3ISPCameraData *data = cameraData(activeCamera_);
	Request *request = info->request;

	if (request->hasPendingBuffers())
		return;

	if (!info->metadataProcessed)
		return;

	if (!info->paramDequeued)
		return;

	data->frameInfo_.destroy(info->frame);

	completeRequest(request);
}

void PipelineHandlerC3ISP::bufferReady(FrameBuffer *buffer)
{
	C3ISPCameraData *data = cameraData(activeCamera_);

	C3ISPFrameInfo *info = data->frameInfo_.find(buffer);
	if (!info)
		return;

	const FrameMetadata &metadata = buffer->metadata();
	Request *request = buffer->request();

	if (metadata.status != FrameMetadata::FrameCancelled) {
		request->metadata().set(controls::SensorTimestamp,
					metadata.timestamp);
	}

	completeBuffer(request, buffer);
	tryCompleteRequest(info);
}

void PipelineHandlerC3ISP::statReady(FrameBuffer *buffer)
{
	C3ISPCameraData *data = cameraData(activeCamera_);

	C3ISPFrameInfo *info = data->frameInfo_.find(buffer);
	if (!info)
		return;

	if (buffer->metadata().status == FrameMetadata::FrameCancelled) {
		info->metadataProcessed = true;
		tryCompleteRequest(info);
		return;
	}

	if (data->frame_ <= buffer->metadata().sequence)
		data->frame_ = buffer->metadata().sequence + 1;

	data->ipa_->processStatsBuffer(info->frame, info->statBuffer->cookie(),
				       data->delayedCtrls_->get(buffer->metadata().sequence));
}

void PipelineHandlerC3ISP::paramReady(FrameBuffer *buffer)
{
	C3ISPCameraData *data = cameraData(activeCamera_);

	C3ISPFrameInfo *info = data->frameInfo_.find(buffer);
	if (!info)
		return;

	info->paramDequeued = true;
	tryCompleteRequest(info);
}

bool PipelineHandlerC3ISP::createCamera(MediaLink *ispLink)
{
	MediaEntity *adap = ispLink->source()->entity();
	const MediaPad *adapSink = adap->getPadByIndex(0);
	MediaEntity *csi = adapSink->links()[0]->source()->entity();
	const MediaPad *csiSink = csi->getPadByIndex(0);
	MediaEntity *sensor = csiSink->links()[0]->source()->entity();

	std::unique_ptr<C3ISPCameraData> data =
		std::make_unique<C3ISPCameraData>(this, sensor);

	if (data->init())
		return false;

	/* Generic values for sensor */
	std::unordered_map<uint32_t, DelayedControls::ControlParams> params = {
		{ V4L2_CID_ANALOGUE_GAIN, { 1, false } },
		{ V4L2_CID_EXPOSURE, { 2, false } },
	};

	data->delayedCtrls_ = std::make_unique<DelayedControls>(data->sensor_->device(), params);
	isp_->frameStart.connect(data->delayedCtrls_.get(), &DelayedControls::applyControls);

	int ret = data->loadIPA(media_->hwRevision());
	if (ret)
		return false;

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

	stat_ = V4L2VideoDevice::fromEntityName(media_, "c3-isp-stats");
	if (stat_->open() < 0)
		return false;

	stat_->bufferReady.connect(this, &PipelineHandlerC3ISP::statReady);

	param_ = V4L2VideoDevice::fromEntityName(media_, "c3-isp-params");
	if (param_->open() < 0)
		return false;

	param_->bufferReady.connect(this, &PipelineHandlerC3ISP::paramReady);

	C3ISPPipe *viewPipe = &pipes_[C3ISPVIEW];
	viewPipe->resizer = V4L2Subdevice::fromEntityName(media_, "c3-isp-resizer0");
	if (viewPipe->resizer->open() < 0)
		return false;

	viewPipe->cap = V4L2VideoDevice::fromEntityName(media_, "c3-isp-cap0");
	if (viewPipe->cap->open() < 0)
		return false;

	viewPipe->cap->bufferReady.connect(this, &PipelineHandlerC3ISP::bufferReady);

	C3ISPPipe *stillPipe = &pipes_[C3ISPSTILL];
	stillPipe->resizer = V4L2Subdevice::fromEntityName(media_, "c3-isp-resizer1");
	if (stillPipe->resizer->open() < 0)
		return false;

	stillPipe->cap = V4L2VideoDevice::fromEntityName(media_, "c3-isp-cap1");
	if (stillPipe->cap->open() < 0)
		return false;

	stillPipe->cap->bufferReady.connect(this, &PipelineHandlerC3ISP::bufferReady);

	C3ISPPipe *videoPipe = &pipes_[C3ISPVIDEO];
	videoPipe->resizer = V4L2Subdevice::fromEntityName(media_, "c3-isp-resizer2");
	if (videoPipe->resizer->open() < 0)
		return false;

	videoPipe->cap = V4L2VideoDevice::fromEntityName(media_, "c3-isp-cap2");
	if (videoPipe->cap->open() < 0)
		return false;

	videoPipe->cap->bufferReady.connect(this, &PipelineHandlerC3ISP::bufferReady);

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
