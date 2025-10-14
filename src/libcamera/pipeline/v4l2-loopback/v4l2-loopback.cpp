/* SPDX-License-Identifier: LGPL-2.1 */
/*
 * Copyright (C) 2020, Google Inc.
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *
 * v4l2-loopback.cpp - Pipeline handler for the capture devices created by
 * v4l2loopback kernel module.
 */

#include <math.h>

#include <libcamera/base/log.h>

#include <libcamera/control_ids.h>
#include <libcamera/controls.h>
#include <libcamera/formats.h>
#include <libcamera/property_ids.h>

#include "libcamera/internal/camera.h"
#include "libcamera/internal/device_enumerator.h"
#include "libcamera/internal/media_device.h"
#include "libcamera/internal/pipeline_handler.h"
#include "libcamera/internal/v4l2_videodevice.h"

/*
 * Explicitly disable the unused-parameter warning in this pipeline handler.
 *
 * Parameters are left unused while they are introduced incrementally, so for
 * documentation purposes only we disable this warning so that we can compile
 * each commit independently without breaking the flow of the development
 * additions.
 *
 * This is not recommended practice within libcamera, please listen to your
 * compiler warnings.
 */
#pragma GCC diagnostic ignored "-Wunused-parameter"

namespace libcamera {

LOG_DEFINE_CATEGORY(V4L2LP)

class V4L2LPCameraData : public Camera::Private
{
public:
	V4L2LPCameraData(PipelineHandler *pipe, MediaDevice *media)
		: Camera::Private(pipe), media_(media), video_(nullptr)
	{
	}

	~V4L2LPCameraData()
	{
	}

	int init(MediaDevice *media);
	void bufferReady(FrameBuffer *buffer);

	MediaDevice *media_;
	std::unique_ptr<V4L2VideoDevice> video_;
	unsigned enqueuedBufs_;
	Stream stream_;
};

class V4L2LPCameraConfiguration : public CameraConfiguration
{
public:
	V4L2LPCameraConfiguration(V4L2LPCameraData *data);

	Status validate() override;

private:
	V4L2LPCameraData *data_;
};

class PipelineHandlerV4L2LP : public PipelineHandler
{
public:
	PipelineHandlerV4L2LP(CameraManager *manager);

	std::unique_ptr<CameraConfiguration>
		generateConfiguration(Camera *camera, Span<const StreamRole> roles) override;
	int configure(Camera *camera, CameraConfiguration *config) override;

	int exportFrameBuffers(Camera *camera, Stream *stream,
			       std::vector<std::unique_ptr<FrameBuffer>> *buffers) override;

	int start(Camera *camera, const ControlList *controls) override;
	void stopDevice(Camera *camera) override;

	int queueRequestDevice(Camera *camera, Request *request) override;

	bool match(DeviceEnumerator *enumerator) override;

private:
	int processControls(V4L2LPCameraData *data, Request *request);

	V4L2LPCameraData *cameraData(Camera *camera)
	{
		return static_cast<V4L2LPCameraData *>(camera->_d());
	}
};

V4L2LPCameraConfiguration::V4L2LPCameraConfiguration(V4L2LPCameraData *data)
	: CameraConfiguration(), data_(data)
{
}

CameraConfiguration::Status V4L2LPCameraConfiguration::validate()
{
	Status status = Valid;

	if (config_.empty())
		return Invalid;

	/* Cap the number of entries to the available streams. */
	if (config_.size() > 1) {
		config_.resize(1);
		status = Adjusted;
	}

	StreamConfiguration &cfg = config_[0];

	/* Adjust the pixel format. */
	const std::vector<libcamera::PixelFormat> formats = cfg.formats().pixelformats();
	if (std::find(formats.begin(), formats.end(), cfg.pixelFormat) == formats.end()) {
		cfg.pixelFormat = cfg.formats().pixelformats()[0];
		LOG(V4L2LP, Debug) << "Adjusting format to " << cfg.pixelFormat.toString();
		status = Adjusted;
	}

	/* Restrictions imposed by qcarcam */
	if (cfg.bufferCount < 4) {
	        cfg.bufferCount = 4;
	} else if (cfg.bufferCount > 20) {
		cfg.bufferCount = 20;
	}

	V4L2DeviceFormat format;
	format.size = cfg.size;
	format.fourcc = data_->video_->toV4L2PixelFormat(cfg.pixelFormat);
	int ret = data_->video_->tryFormat(&format);
	if (ret) {
		/*
		 * If format is not supported, then use the current one which
		 * which is normally the only option for ais server proxy.
		 */
		ret = data_->video_->getFormat(&format);
		if (ret) {
			return Invalid;
		}
		cfg.pixelFormat = format.fourcc.toPixelFormat();
		cfg.size = format.size;
		status = Adjusted;
	}
	cfg.stride = format.planes[0].bpl;
	cfg.frameSize = format.planes[0].size;

	if (cfg.colorSpace != format.colorSpace) {
		cfg.colorSpace = format.colorSpace;
		status = Adjusted;
	}

	return status;
}

PipelineHandlerV4L2LP::PipelineHandlerV4L2LP(CameraManager *manager)
	: PipelineHandler(manager)
{
}

std::unique_ptr<CameraConfiguration>
PipelineHandlerV4L2LP::generateConfiguration(Camera *camera, Span<const StreamRole> roles)
{
	V4L2LPCameraData *data = cameraData(camera);
	std::unique_ptr<CameraConfiguration> config =
	       std::make_unique<V4L2LPCameraConfiguration>(data);

	if (roles.empty())
		return config;

	std::map<V4L2PixelFormat, std::vector<SizeRange>> v4l2Formats =
		data->video_->formats();
	std::map<PixelFormat, std::vector<SizeRange>> deviceFormats;
	std::transform(v4l2Formats.begin(), v4l2Formats.end(),
		       std::inserter(deviceFormats, deviceFormats.begin()),
		       [&](const decltype(v4l2Formats)::value_type &format) {
			       return decltype(deviceFormats)::value_type{
				       format.first.toPixelFormat(),
				       format.second
			       };
		       });

	StreamFormats formats(deviceFormats);
	StreamConfiguration cfg(formats);

	cfg.pixelFormat = formats::UYVY;
	cfg.size = { 1920, 1080 };
	cfg.bufferCount = 4;

	config->addConfiguration(cfg);

	config->validate();

	return config;
}

int PipelineHandlerV4L2LP::configure(Camera *camera, CameraConfiguration *config)
{
	V4L2LPCameraData *data = cameraData(camera);
	StreamConfiguration &cfg = config->at(0);
	int ret;

	V4L2DeviceFormat format = {};
	format.fourcc = data->video_->toV4L2PixelFormat(cfg.pixelFormat);
	format.size = cfg.size;

	ret = data->video_->setFormat(&format);
	if (ret)
		return ret;

	if (format.size != cfg.size ||
	    format.fourcc != data->video_->toV4L2PixelFormat(cfg.pixelFormat)) {
		LOG(V4L2LP, Error)
			<< "Requested " << cfg.toString() << ", got "
			<< format.size.toString() << "-"
			<< format.fourcc.toString();
		return -EINVAL;
	}

	/* Set initial controls specific to V4L2 loopback */
	ControlList controls(data->video_->controls());
	ret = data->video_->setControls(&controls);
	if (ret) {
		LOG(V4L2LP, Error) << "Failed to set controls: " << ret;
		return ret < 0 ? ret : -EINVAL;
	}

	cfg.setStream(&data->stream_);
	cfg.stride = format.planes[0].bpl;

	return 0;
}

int PipelineHandlerV4L2LP::exportFrameBuffers(Camera *camera, Stream *stream,
					     std::vector<std::unique_ptr<FrameBuffer>> *buffers)
{
	V4L2LPCameraData *data = cameraData(camera);
	unsigned int count = stream->configuration().bufferCount;

	return data->video_->exportBuffers(count, buffers);
}

int PipelineHandlerV4L2LP::start(Camera *camera, const ControlList *controls)
{
	V4L2LPCameraData *data = cameraData(camera);
	unsigned int count = data->stream_.configuration().bufferCount;
	int ret;

	ret = data->video_->importBuffers(count);
	if (ret < 0)
		return ret;

	data->enqueuedBufs_ = 0;

	return 0;
}

void PipelineHandlerV4L2LP::stopDevice(Camera *camera)
{
	V4L2LPCameraData *data = cameraData(camera);
	data->video_->streamOff();
	data->video_->releaseBuffers();
	data->enqueuedBufs_ = 0;
}

int PipelineHandlerV4L2LP::processControls(V4L2LPCameraData *data, Request *request)
{
	ControlList controls(data->video_->controls());

	for (auto it : request->controls()) {
		unsigned int id = it.first;
		unsigned int offset;
		uint32_t cid;

		if (id == controls::Brightness) {
			cid = V4L2_CID_BRIGHTNESS;
			offset = 128;
		} else if (id == controls::Contrast) {
			cid = V4L2_CID_CONTRAST;
			offset = 0;
		} else if (id == controls::Saturation) {
			cid = V4L2_CID_SATURATION;
			offset = 0;
		} else {
			continue;
		}

		int32_t value = lroundf(it.second.get<float>() * 128 + offset);
		controls.set(cid, std::clamp(value, 0, 255));
	}

	for (const auto &ctrl : controls)
		LOG(V4L2LP, Debug)
			<< "Setting control " << utils::hex(ctrl.first)
			<< " to " << ctrl.second.toString();

	int ret = data->video_->setControls(&controls);
	if (ret) {
		LOG(V4L2LP, Error) << "Failed to set controls: " << ret;
		return ret < 0 ? ret : -EINVAL;
	}

	return ret;
}

int PipelineHandlerV4L2LP::queueRequestDevice(Camera *camera, Request *request)
{
	V4L2LPCameraData *data = cameraData(camera);
	FrameBuffer *buffer = request->findBuffer(&data->stream_);
	if (!buffer) {
		LOG(V4L2LP, Error)
			<< "Attempt to queue request with invalid stream";

		return -ENOENT;
	}

	int ret = processControls(data, request);
	if (ret < 0)
		return ret;

	ret = data->video_->queueBuffer(buffer);
	if (ret < 0)
		return ret;

	if (data->enqueuedBufs_ < data->stream_.configuration().bufferCount) {
		data->enqueuedBufs_++;
		if (data->enqueuedBufs_ == data->stream_.configuration().bufferCount) {
			ret = data->video_->streamOn();
			if (ret < 0) {
				data->video_->releaseBuffers();
				return ret;
			}
		}
	}

	return 0;
}

bool PipelineHandlerV4L2LP::match(DeviceEnumerator *enumerator)
{
	MediaDevice *media;
	std::unique_ptr<V4L2LPCameraData> data;
	int ret = 0;
	DeviceMatch dm("v4l2loopback");

	/*
	 * Acquire every matching v4l2lp dev and ignore those that couldn't
	 * initialize. This way, CameraManager won't bail on enumerating the
	 * rest of devices after one failure.
	 */
	while ((media = acquireMediaDevice(enumerator, dm))) {
		data = std::make_unique<V4L2LPCameraData>(this, media);
		ret = data->init(media);
		if (!ret)
			break;
	}
	if (!media || ret)
		return false;

	std::set<Stream *> streams{ &data->stream_ };
	const std::string id = data->video_->busName();
	std::shared_ptr<Camera> camera = Camera::create(std::move(data), id, streams);
	registerCamera(std::move(camera));

	return true;
}

int V4L2LPCameraData::init(MediaDevice *media)
{

        /* Locate and initialise the camera data with the default video node. */
        const std::vector<MediaEntity *> &entities = media->entities();
        auto entity = std::find_if(entities.begin(), entities.end(),
                                   [](MediaEntity *e) {
                                           return 1;
                                   });
        if (entity == entities.end()) {
                LOG(V4L2LP, Error) << "Could not find a default video device";
                return -ENODEV;
        }

        video_ = std::make_unique<V4L2VideoDevice>(*entity);

	if (video_->open()) {
		return -ENODEV;
	}

	video_->bufferReady.connect(this, &V4L2LPCameraData::bufferReady);

	/* Initialise the supported controls and properties. */
	const ControlInfoMap &controls = video_->controls();
	ControlInfoMap::Map ctrls;

	for (const auto &ctrl : controls) {
		const ControlId *id;
		ControlInfo info;

		switch (ctrl.first->id()) {
		case V4L2_CID_BRIGHTNESS:
			id = &controls::Brightness;
			info = ControlInfo{ { -1.0f }, { 1.0f }, { 0.0f } };
			break;
		case V4L2_CID_CONTRAST:
			id = &controls::Contrast;
			info = ControlInfo{ { 0.0f }, { 2.0f }, { 1.0f } };
			break;
		case V4L2_CID_SATURATION:
			id = &controls::Saturation;
			info = ControlInfo{ { 0.0f }, { 2.0f }, { 1.0f } };
			break;
		default:
			continue;
		}

		ctrls.emplace(id, info);
	}

	controlInfo_ = ControlInfoMap(std::move(ctrls), controls::controls);

	properties_.set(properties::Location, properties::CameraLocationExternal);
	properties_.set(properties::Model, "Virtual Video Device");

	return 0;
}

void V4L2LPCameraData::bufferReady(FrameBuffer *buffer)
{
	Request *request = buffer->request();

	/* Record the sensor's timestamp in the request metadata. */
	request->metadata().set(controls::SensorTimestamp,
				buffer->metadata().timestamp);

	pipe()->completeBuffer(request, buffer);
	pipe()->completeRequest(request);
}

REGISTER_PIPELINE_HANDLER(PipelineHandlerV4L2LP, "v4l2-loopback")

} /* namespace libcamera */
