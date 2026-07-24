/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * Copyright (C) 2026, Magdum <magdum.foss@gmail.com>
 *
 * Pipeline handler for NVIDIA Tegra VI (Video Input)
 *
 * Supports Jetson platforms (Nano / TX2 / Xavier / Orin) using the
 * upstream tegra-video V4L2 / Media Controller driver.
 *
 * Pipeline topology
 * -----------------
 *
 *   [Camera Sensor (I2C)]
 *          |  (MIPI CSI-2)
 *   [tegra-csi  V4L2 subdev]    <-- sets Bayer bus format and lane count
 *          |
 *   [tegra-vi   V4L2 capture]   <-- DMA of raw Bayer frames into memory
 *          |
 *   [SoftwareIsp]               <-- debayer, AGC, AWB, CCM, gamma
 *          |
 *   [User FrameBuffer]
 *
 * The capture video device captures raw Bayer frames into a small ring of
 * internal buffers.  Each completed raw frame is handed to SoftwareIsp,
 * which debayers it into the user-supplied output buffer and then
 * completes the associated Request.
 *
 * Hardware ISP support (ISP7 on Tegra234) can be added in a follow-up
 * once the in-kernel tegra-isp driver is upstreamed.
 */

#include <algorithm>
#include <map>
#include <memory>
#include <queue>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

#include <libcamera/base/log.h>

#include <libcamera/camera.h>
#include <libcamera/color_space.h>
#include <libcamera/control_ids.h>
#include <libcamera/formats.h>
#include <libcamera/geometry.h>
#include <libcamera/pixel_format.h>
#include <libcamera/stream.h>

#include "libcamera/internal/bayer_format.h"
#include "libcamera/internal/camera.h"
#include "libcamera/internal/camera_sensor.h"
#include "libcamera/internal/camera_sensor_properties.h"
#include "libcamera/internal/delayed_controls.h"
#include "libcamera/internal/device_enumerator.h"
#include "libcamera/internal/formats.h"
#include "libcamera/internal/media_device.h"
#include "libcamera/internal/pipeline_handler.h"
#include "libcamera/internal/request.h"
#include "libcamera/internal/software_isp/software_isp.h"
#include "libcamera/internal/v4l2_subdevice.h"
#include "libcamera/internal/v4l2_videodevice.h"

namespace libcamera {

LOG_DEFINE_CATEGORY(TegraPipeline)

/* Number of internal raw capture buffers in the VI ring. */
static constexpr unsigned int kNumRawBuffers = 4;

/*
 * Select the smallest sensor size that covers the requested output size.
 * Falls back to the largest available size if none is large enough.
 */
static bool selectSensorSize(const std::vector<Size> &sensorSizes,
			     const Size &requested, Size *sensorSize)
{
	if (sensorSizes.empty())
		return false;

	for (const Size &size : sensorSizes) {
		if (size.width >= requested.width &&
		    size.height >= requested.height) {
			*sensorSize = size;
			return true;
		}
	}

	*sensorSize = sensorSizes.back();
	return true;
}

/* -------------------------------------------------------------------------
 * Per-frame bookkeeping
 * -------------------------------------------------------------------------
 *
 * We need to know, for every in-flight frame sequence number:
 *   - which Request it belongs to
 *   - whether the SoftwareIsp will emit metadata (so we wait for it)
 *   - whether that metadata has already arrived
 */
struct TegraFrameInfo {
	TegraFrameInfo(Request *r, bool m)
		: request(r), metadataRequired(m),
		  metadataProcessed(false)
	{
	}

	Request *request;
	bool metadataRequired;
	bool metadataProcessed;
};

/* -------------------------------------------------------------------------
 * Forward declarations
 * -------------------------------------------------------------------------*/
class PipelineHandlerTegra;

/* -------------------------------------------------------------------------
 * TegraCameraData
 * -------------------------------------------------------------------------
 *
 * Per-camera state: owns the sensor, CSI subdev, VI video device and the
 * software ISP instance.
 */
class TegraCameraData : public Camera::Private
{
public:
	TegraCameraData(PipelineHandlerTegra *pipe,
			std::shared_ptr<MediaDevice> media);

	int init();

	/* Callbacks */
	void imageBufferReady(FrameBuffer *buffer);
	void ispOutputBufferReady(FrameBuffer *buffer);
	void clearIncompleteRequests();
	void ispStatsReady(uint32_t frame, uint32_t bufferId);
	void metadataReady(uint32_t frame, const ControlList &metadata);
	void setSensorControls(const ControlList &sensorControls);

	PipelineHandlerTegra *pipe();

	/* Hardware resources */
	std::shared_ptr<MediaDevice> media_;
	std::unique_ptr<CameraSensor> sensor_;
	std::unique_ptr<V4L2Subdevice> csi_;
	std::unique_ptr<V4L2VideoDevice> video_;
	const MediaEntity *videoEntity_;

	/* Software ISP (debayer + IPA) */
	std::unique_ptr<SoftwareIsp> swIsp_;

	/* Delayed sensor controls (exposure / gain) */
	std::unique_ptr<DelayedControls> delayedCtrls_;

	/* The single output stream exposed to the application */
	Stream stream_;

	/*
	 * Ring of internal raw capture buffers.
	 * These are allocated once at start() and pre-queued to the VI node.
	 */
	std::vector<std::unique_ptr<FrameBuffer>> rawBuffers_;

	/*
	 * Queue of pending requests: each entry pairs a Request with the
	 * user-supplied output FrameBuffer.  The head entry is consumed when
	 * the next raw frame arrives from the VI device.
	 */
	struct PendingRequest {
		Request *request;
		FrameBuffer *outputBuffer;
	};
	std::queue<PendingRequest> pendingRequests_;

	/* Per-frame metadata tracking */
	std::map<uint32_t, TegraFrameInfo> frameInfos_;

private:
	void tryCompleteRequest(Request *request);
};

/* -------------------------------------------------------------------------
 * TegraCameraConfiguration
 * -------------------------------------------------------------------------
 *
 * Validates and adjusts the stream configuration requested by the
 * application.  Only a single output stream is supported.
 */
class TegraCameraConfiguration : public CameraConfiguration
{
public:
	TegraCameraConfiguration(Camera *camera, TegraCameraData *data);

	Status validate() override;

	static constexpr unsigned int kNumBuffersDefault = 4;
	static constexpr unsigned int kNumBuffersMax = 32;

private:
	std::shared_ptr<Camera> camera_;
	TegraCameraData *data_;
};

/* -------------------------------------------------------------------------
 * PipelineHandlerTegra
 * -------------------------------------------------------------------------*/
class PipelineHandlerTegra : public PipelineHandler
{
public:
	explicit PipelineHandlerTegra(CameraManager *manager);

	std::unique_ptr<CameraConfiguration>
	generateConfiguration(Camera *camera,
			      Span<const StreamRole> roles) override;

	int configure(Camera *camera, CameraConfiguration *config) override;

	int exportFrameBuffers(Camera *camera, Stream *stream,
			       std::vector<std::unique_ptr<FrameBuffer>> *buffers) override;

	int start(Camera *camera, const ControlList *controls) override;
	void stopDevice(Camera *camera) override;

	bool match(DeviceEnumerator *enumerator) override;

protected:
	int queueRequestDevice(Camera *camera, Request *request) override;

private:
	TegraCameraData *cameraData(Camera *camera)
	{
		return static_cast<TegraCameraData *>(camera->_d());
	}

	/*
	 * Traverse the media graph starting from a camera sensor entity and
	 * return the first CSI bridge and VI capture node found on the path.
	 * Returns true on success.
	 */
	static bool findPipelineEntities(MediaEntity *sensorEntity,
					 MediaEntity **csiEntity,
					 MediaEntity **viEntity);

	bool createCamera(std::shared_ptr<MediaDevice> media,
			  MediaEntity *sensorEntity);
};

/* =========================================================================
 * TegraCameraData implementation
 * =========================================================================*/

TegraCameraData::TegraCameraData(PipelineHandlerTegra *pipe,
				 std::shared_ptr<MediaDevice> media)
	: Camera::Private(pipe), media_(media), videoEntity_(nullptr)
{
}

PipelineHandlerTegra *TegraCameraData::pipe()
{
	return static_cast<PipelineHandlerTegra *>(Camera::Private::pipe());
}

/*
 * Open all V4L2 devices, create the CameraSensor and SoftwareIsp instances,
 * and set up the DelayedControls helper.
 */
int TegraCameraData::init()
{
	int ret;

	/* Open the CSI bridge subdev. */
	ret = csi_->open();
	if (ret) {
		LOG(TegraPipeline, Error)
			<< "Failed to open CSI subdev: " << ret;
		return ret;
	}

	/* Open the VI video device. */
	ret = video_->open();
	if (ret) {
		LOG(TegraPipeline, Error)
			<< "Failed to open VI video device: " << ret;
		return ret;
	}

	/* Build the DelayedControls helper for sensor exposure / gain. */
	const CameraSensorProperties::SensorDelays &delays =
		sensor_->sensorDelays();

	std::unordered_map<uint32_t, DelayedControls::ControlParams> params = {
		{ V4L2_CID_ANALOGUE_GAIN,
		  { delays.gainDelay, false } },
		{ V4L2_CID_EXPOSURE,
		  { delays.exposureDelay, false } },
	};
	delayedCtrls_ =
		std::make_unique<DelayedControls>(sensor_->device(), params);

	/* Create the Software ISP (provides debayering + AGC/AWB via IPA). */
	swIsp_ = std::make_unique<SoftwareIsp>(pipe(), sensor_.get(),
					       &controlInfo_);
	if (!swIsp_->isValid()) {
		LOG(TegraPipeline, Error) << "Failed to create software ISP";
		swIsp_.reset();
		return -ENODEV;
	}

	/* Wire software ISP signals. */
	swIsp_->outputBufferReady.connect(this,
					  &TegraCameraData::ispOutputBufferReady);
	swIsp_->ispStatsReady.connect(this, &TegraCameraData::ispStatsReady);
	swIsp_->metadataReady.connect(this, &TegraCameraData::metadataReady);
	swIsp_->setSensorControls.connect(this,
					  &TegraCameraData::setSensorControls);

	return 0;
}

/* Called by the VI video device when a raw frame DMA is complete. */
void TegraCameraData::imageBufferReady(FrameBuffer *buffer)
{
	PipelineHandlerTegra *pipe = TegraCameraData::pipe();

	/* On error or cancellation just complete the request immediately. */
	if (buffer->metadata().status != FrameMetadata::FrameSuccess) {
		if (pendingRequests_.empty()) {
			/* Stale buffer with no associated request – requeue. */
			video_->queueBuffer(buffer);
			return;
		}

		PendingRequest &pending = pendingRequests_.front();
		Request *request = pending.request;

		auto it = frameInfos_.find(request->sequence());
		if (it != frameInfos_.end())
			it->second.metadataRequired = false;

		if (pipe->completeBuffer(request, pending.outputBuffer))
			tryCompleteRequest(request);
		pendingRequests_.pop();

		/*
		 * If the buffer was cancelled we stop; otherwise it is an
		 * error frame so requeue the internal buffer for the next
		 * capture.
		 */
		if (buffer->metadata().status != FrameMetadata::FrameCancelled)
			video_->queueBuffer(buffer);

		return;
	}

	/* Stamp the sensor timestamp into request metadata via the correct sequence lookup. */
	if (!pendingRequests_.empty()) {
		Request *request = pendingRequests_.front().request;
		auto it = frameInfos_.find(request->sequence());
		if (it != frameInfos_.end()) {
			request->_d()->metadata().set(controls::SensorTimestamp,
						      buffer->metadata().timestamp);
		}
	}

	/* No pending request: requeue the internal buffer and wait. */
	if (pendingRequests_.empty()) {
		video_->queueBuffer(buffer);
		return;
	}

	/* Hand the raw frame and the output buffer to the software ISP. */
	PendingRequest &pending = pendingRequests_.front();
	std::map<const Stream *, FrameBuffer *> outputs{
		{ &stream_, pending.outputBuffer }
	};

	const int ret = swIsp_->queueBuffers(pending.request->sequence(),
					     buffer, outputs);
	if (ret) {
		LOG(TegraPipeline, Error)
			<< "Failed to queue buffers to software ISP: " << ret;
		if (auto it = frameInfos_.find(pending.request->sequence());
		    it != frameInfos_.end())
			it->second.metadataRequired = false;
		if (pipe->completeBuffer(pending.request, pending.outputBuffer))
			tryCompleteRequest(pending.request);
		pendingRequests_.pop();
		video_->queueBuffer(buffer);
		return;
	}

	pendingRequests_.pop();
}

void TegraCameraData::ispStatsReady(uint32_t frame, uint32_t bufferId)
{
	swIsp_->processStats(frame, bufferId, delayedCtrls_->get(frame));
}

void TegraCameraData::metadataReady(uint32_t frame,
				    const ControlList &metadata)
{
	auto it = frameInfos_.find(frame);
	if (it == frameInfos_.end())
		return;

	TegraFrameInfo &info = it->second;
	info.request->_d()->metadata().merge(metadata);
	info.metadataProcessed = true;
	tryCompleteRequest(info.request);
}

void TegraCameraData::setSensorControls(
	const ControlList &sensorControls)
{
	delayedCtrls_->push(sensorControls);
}

void TegraCameraData::ispOutputBufferReady(FrameBuffer *buffer)
{
	Request *request = buffer->request();
	if (!request)
		return;

	if (pipe()->completeBuffer(request, buffer))
		tryCompleteRequest(request);
}

void TegraCameraData::tryCompleteRequest(Request *request)
{
	if (request->hasPendingBuffers())
		return;

	auto it = frameInfos_.find(request->sequence());
	if (it == frameInfos_.end())
		return;

	const TegraFrameInfo &info = it->second;
	if (info.metadataRequired && !info.metadataProcessed)
		return;

	frameInfos_.erase(it);
	pipe()->completeRequest(request);
}

void TegraCameraData::clearIncompleteRequests()
{
	while (!pendingRequests_.empty()) {
		pipe()->cancelRequest(pendingRequests_.front().request);
		pendingRequests_.pop();
	}
}

/* =========================================================================
 * TegraCameraConfiguration implementation
 * =========================================================================*/

TegraCameraConfiguration::TegraCameraConfiguration(Camera *camera,
						   TegraCameraData *data)
	: CameraConfiguration(),
	  camera_(camera->shared_from_this()),
	  data_(data)
{
}

CameraConfiguration::Status TegraCameraConfiguration::validate()
{
	Status status = Valid;

	if (config_.empty())
		return Invalid;

	/* Only a single output stream is supported. */
	if (config_.size() > 1) {
		config_.resize(1);
		status = Adjusted;
	}

	/*
	 * Fix the orientation: the Tegra VI pipeline does not support
	 * hardware transforms, so only Rotate0 is valid.
	 */
	if (orientation != Orientation::Rotate0) {
		orientation = Orientation::Rotate0;
		status = Adjusted;
	}

	StreamConfiguration &cfg = config_[0];

	/* Validate that the requested pixel format is supported by swIsp. */
	if (!data_->swIsp_)
		return Invalid;

	/*
	 * Use the first sensor format to probe what swIsp can produce, then
	 * check whether the requested format is in that list.
	 */
	const std::vector<uint32_t> mbusCodes = data_->sensor_->mbusCodes();
	if (mbusCodes.empty())
		return Invalid;

	const uint32_t primaryCode = mbusCodes.front();
	const std::vector<Size> &sensorSizes = data_->sensor_->sizes(primaryCode);
	if (sensorSizes.empty())
		return Invalid;

	const Size &maxSensorSize = sensorSizes.back();
	const BayerFormat bayerFmt = BayerFormat::fromMbusCode(primaryCode);
	const std::vector<PixelFormat> supportedFormats =
		data_->swIsp_->formats(
			bayerFmt.isValid() ? bayerFmt.toPixelFormat()
					   : formats::SRGGB10);

	if (!supportedFormats.empty()) {
		auto it = std::find(supportedFormats.begin(),
				    supportedFormats.end(),
				    cfg.pixelFormat);
		if (it == supportedFormats.end()) {
			cfg.pixelFormat = supportedFormats.front();
			LOG(TegraPipeline, Debug)
				<< "Adjusting pixel format to "
				<< cfg.pixelFormat;
			status = Adjusted;
		}
	}

	/* Clamp size to sensor output range. */
	SizeRange outputSizes =
		data_->swIsp_->sizes(
			bayerFmt.isValid() ? bayerFmt.toPixelFormat()
					   : formats::SRGGB10,
			maxSensorSize);

	if (!outputSizes.contains(cfg.size)) {
		cfg.size = cfg.size.boundedTo(outputSizes.max)
				   .expandedTo(outputSizes.min);
		LOG(TegraPipeline, Debug)
			<< "Adjusting size to " << cfg.size;
		status = Adjusted;
	}

	/* Fix up color space. */
	if (!cfg.colorSpace) {
		const PixelFormatInfo &info =
			PixelFormatInfo::info(cfg.pixelFormat);
		switch (info.colourEncoding) {
		case PixelFormatInfo::ColourEncodingRGB:
			cfg.colorSpace = ColorSpace::Srgb;
			break;
		case PixelFormatInfo::ColourEncodingYUV:
			cfg.colorSpace = ColorSpace::Sycc;
			break;
		default:
			cfg.colorSpace = ColorSpace::Raw;
			break;
		}
	}
	cfg.colorSpace->adjust(cfg.pixelFormat);

	/* Determine stride and frame size via swIsp. */
	auto [stride, frameSize] =
		data_->swIsp_->strideAndFrameSize(cfg.pixelFormat, cfg.size);
	if (stride == 0)
		return Invalid;

	cfg.stride = stride;
	cfg.frameSize = frameSize;

	if (!cfg.bufferCount)
		cfg.bufferCount = TegraCameraConfiguration::kNumBuffersDefault;
	else if (cfg.bufferCount > TegraCameraConfiguration::kNumBuffersMax)
		cfg.bufferCount = TegraCameraConfiguration::kNumBuffersMax;

	return status;
}

/* =========================================================================
 * PipelineHandlerTegra implementation
 * =========================================================================*/

PipelineHandlerTegra::PipelineHandlerTegra(CameraManager *manager)
	: PipelineHandler(manager)
{
}

/* ---------------------------------------------------------------------- */
/*  generateConfiguration                                                  */
/* ---------------------------------------------------------------------- */

std::unique_ptr<CameraConfiguration>
PipelineHandlerTegra::generateConfiguration(Camera *camera,
					    Span<const StreamRole> roles)
{
	TegraCameraData *data = cameraData(camera);

	auto config = std::make_unique<TegraCameraConfiguration>(camera, data);

	if (roles.empty())
		return config;

	/*
	 * Build the set of output formats / sizes that swIsp can produce
	 * for the primary sensor media bus code.
	 */
	if (!data->swIsp_ || data->sensor_->mbusCodes().empty())
		return nullptr;

	const uint32_t primaryCode = data->sensor_->mbusCodes().front();
	const BayerFormat bayer = BayerFormat::fromMbusCode(primaryCode);
	if (!bayer.isValid())
		return nullptr;
	const PixelFormat bayerFmt = bayer.toPixelFormat();

	const std::vector<PixelFormat> outFormats =
		data->swIsp_->formats(bayerFmt);
	if (outFormats.empty())
		return nullptr;

	/* Use the largest available sensor size for the default. */
	const std::vector<Size> &sensorSizes =
		data->sensor_->sizes(primaryCode);
	if (sensorSizes.empty())
		return nullptr;

	const Size &maxSize = sensorSizes.back();
	SizeRange outSizes = data->swIsp_->sizes(bayerFmt, maxSize);

	/* Build StreamFormats map for the processed stream. */
	std::map<PixelFormat, std::vector<SizeRange>> fmtMap;
	for (const PixelFormat &fmt : outFormats)
		fmtMap[fmt] = { outSizes };

	/*
	 * Only a single output stream is supported; silently accept any role
	 * and return one stream configuration.
	 */
	StreamConfiguration cfg{ StreamFormats{ fmtMap } };
	cfg.pixelFormat = outFormats.front();
	cfg.size = outSizes.max;
	cfg.bufferCount = TegraCameraConfiguration::kNumBuffersDefault;

	config->addConfiguration(cfg);
	config->validate();

	return config;
}

/* ---------------------------------------------------------------------- */
/*  configure                                                              */
/* ---------------------------------------------------------------------- */

int PipelineHandlerTegra::configure(Camera *camera,
				    CameraConfiguration *c)
{
	TegraCameraData *data = cameraData(camera);
	StreamConfiguration &cfg = c->at(0);
	int ret;

	/*
	 * Disable all media links on the board to start from a clean state,
	 * then re-enable the sensor → CSI → VI path.
	 */
	ret = data->media_->disableLinks();
	if (ret) {
		LOG(TegraPipeline, Error)
			<< "Failed to disable media links: " << ret;
		return ret;
	}

	/* Enable sensor → CSI link. */
	const MediaEntity *sensorEntity = data->sensor_->entity();
	for (MediaPad *pad : sensorEntity->pads()) {
		if (!(pad->flags() & MEDIA_PAD_FL_SOURCE))
			continue;
		for (MediaLink *link : pad->links()) {
			if (link->sink()->entity() ==
			    data->csi_->entity()) {
				ret = link->setEnabled(true);
				if (ret)
					return ret;
				break;
			}
		}
	}

	/* Enable CSI → VI link. */
	for (MediaPad *pad : data->csi_->entity()->pads()) {
		if (!(pad->flags() & MEDIA_PAD_FL_SOURCE))
			continue;
		for (MediaLink *link : pad->links()) {
			if (link->sink()->entity() ==
			    data->videoEntity_) {
				ret = link->setEnabled(true);
				if (ret)
					return ret;
				break;
			}
		}
	}

	/*
	 * Select the best sensor format: smallest resolution that covers
	 * the requested output size.
	 */
	if (data->sensor_->mbusCodes().empty()) {
		LOG(TegraPipeline, Error) << "Sensor has no media bus codes";
		return -EINVAL;
	}

	const uint32_t primaryCode = data->sensor_->mbusCodes().front();
	const std::vector<Size> &sensorSizes = data->sensor_->sizes(primaryCode);
	Size sensorSize;
	if (!selectSensorSize(sensorSizes, cfg.size, &sensorSize)) {
		LOG(TegraPipeline, Error) << "Sensor has no supported sizes";
		return -EINVAL;
	}

	V4L2SubdeviceFormat sensorFmt{};
	sensorFmt.code = primaryCode;
	sensorFmt.size = sensorSize;

	ret = data->sensor_->setFormat(&sensorFmt);
	if (ret) {
		LOG(TegraPipeline, Error)
			<< "Failed to set sensor format: " << ret;
		return ret;
	}

	/*
	 * Propagate the format through the CSI bridge.
	 * Set it on sink pad 0, then read back the source pad 1 format.
	 */
	ret = data->csi_->setFormat(0, &sensorFmt);
	if (ret) {
		LOG(TegraPipeline, Error)
			<< "Failed to set CSI sink format: " << ret;
		return ret;
	}

	V4L2SubdeviceFormat csiFmt{};
	ret = data->csi_->getFormat(1, &csiFmt);
	if (ret) {
		LOG(TegraPipeline, Error)
			<< "Failed to get CSI source format: " << ret;
		return ret;
	}

	/*
	 * Set the raw Bayer format on the VI capture node.
	 * The stride hint from swIsp allows it to use its preferred layout.
	 */
	const BayerFormat captureBayer = BayerFormat::fromMbusCode(csiFmt.code);
	if (!captureBayer.isValid()) {
		LOG(TegraPipeline, Error)
			<< "CSI source format is not a Bayer format: "
			<< csiFmt.code;
		return -EINVAL;
	}
	const PixelFormat captureFmt = captureBayer.toPixelFormat();

	V4L2DeviceFormat videoFmt{};
	videoFmt.fourcc = data->video_->toV4L2PixelFormat(captureFmt);
	videoFmt.size = csiFmt.size;
	videoFmt.planes[0].bpl =
		data->swIsp_->preferredInputStride(captureFmt, csiFmt.size);

	ret = data->video_->setFormat(&videoFmt);
	if (ret) {
		LOG(TegraPipeline, Error)
			<< "Failed to set VI format: " << ret;
		return ret;
	}

	/* Configure the software ISP using the dynamically negotiated buffer count. */
	StreamConfiguration inputCfg{};
	inputCfg.pixelFormat = captureFmt;
	inputCfg.size = csiFmt.size;
	inputCfg.stride = videoFmt.planes[0].bpl;
	inputCfg.bufferCount = cfg.bufferCount;

	ipa::soft::IPAConfigInfo ipaConfig{};
	ipaConfig.sensorControls = data->sensor_->controls();

	std::vector<std::reference_wrapper<const StreamConfiguration>>
		outputCfgs{ cfg };

	ret = data->swIsp_->configure(inputCfg, outputCfgs, ipaConfig);
	if (ret) {
		LOG(TegraPipeline, Error)
			<< "Failed to configure software ISP: " << ret;
		return ret;
	}

	cfg.setStream(&data->stream_);

	LOG(TegraPipeline, Debug)
		<< "Configured: sensor=" << sensorFmt
		<< " CSI=" << csiFmt
		<< " capture=" << captureFmt
		<< "@" << csiFmt.size
		<< " -> output=" << cfg.pixelFormat
		<< "@" << cfg.size;

	return 0;
}

/* ---------------------------------------------------------------------- */
/*  exportFrameBuffers                                                     */
/* ---------------------------------------------------------------------- */

int PipelineHandlerTegra::exportFrameBuffers(
	Camera *camera, Stream *stream,
	std::vector<std::unique_ptr<FrameBuffer>> *buffers)
{
	TegraCameraData *data = cameraData(camera);
	unsigned int count = stream->configuration().bufferCount;
	return data->swIsp_->exportBuffers(stream, count, buffers);
}

/* ---------------------------------------------------------------------- */
/*  start                                                                  */
/* ---------------------------------------------------------------------- */

int PipelineHandlerTegra::start(Camera *camera,
				[[maybe_unused]] const ControlList *controls)
{
	TegraCameraData *data = cameraData(camera);
	int ret;

	/* Allocate internal raw ring-buffer. */
	ret = data->video_->allocateBuffers(kNumRawBuffers,
					    &data->rawBuffers_);
	if (ret < static_cast<int>(kNumRawBuffers)) {
		LOG(TegraPipeline, Error)
			<< "Failed to allocate raw buffers: " << ret;
		return ret < 0 ? ret : -ENOMEM;
	}

	/* Connect the VI buffer-ready signal. */
	data->video_->bufferReady.connect(data,
					  &TegraCameraData::imageBufferReady);

	/* Connect the swIsp input-buffer-ready signal to requeue raw buffers. */
	data->swIsp_->inputBufferReady.connect(
		data->video_.get(),
		[data](FrameBuffer *buf) {
			data->video_->queueBuffer(buf);
		});

	/* Start the software ISP worker. */
	ret = data->swIsp_->start();
	if (ret) {
		LOG(TegraPipeline, Error)
			<< "Failed to start software ISP: " << ret;
		data->video_->bufferReady.disconnect();
		data->swIsp_->inputBufferReady.disconnect();
		data->video_->releaseBuffers();
		return ret;
	}

	data->delayedCtrls_->reset();

	/* Start the VI video device. */
	ret = data->video_->streamOn();
	if (ret) {
		LOG(TegraPipeline, Error)
			<< "Failed to start VI streaming: " << ret;
		data->swIsp_->stop();
		data->video_->bufferReady.disconnect();
		data->swIsp_->inputBufferReady.disconnect();
		data->video_->releaseBuffers();
		return ret;
	}

	/* Pre-queue all internal raw buffers to keep the DMA pipeline full. */
	for (auto &buf : data->rawBuffers_)
		data->video_->queueBuffer(buf.get());

	return 0;
}

/* ---------------------------------------------------------------------- */
/*  stopDevice                                                             */
/* ---------------------------------------------------------------------- */

void PipelineHandlerTegra::stopDevice(Camera *camera)
{
	TegraCameraData *data = cameraData(camera);

	/* Disconnect signals immediately so no late asynchronous callbacks fire. */
	data->video_->bufferReady.disconnect(data, &TegraCameraData::imageBufferReady);
	data->swIsp_->inputBufferReady.disconnect();
	data->swIsp_->outputBufferReady.disconnect();

	/* Gracefully stop worker processing threads and clear active pipelines. */
	data->swIsp_->stop();
	data->video_->streamOff();

	/* Safely release buffers and clear state tracking structures. */
	data->video_->releaseBuffers();
	data->rawBuffers_.clear();
	data->clearIncompleteRequests();
	data->frameInfos_.clear();
}

/* ---------------------------------------------------------------------- */
/*  queueRequestDevice                                                     */
/* ---------------------------------------------------------------------- */

int PipelineHandlerTegra::queueRequestDevice(Camera *camera,
					     Request *request)
{
	TegraCameraData *data = cameraData(camera);

	FrameBuffer *outBuf = request->findBuffer(&data->stream_);
	if (!outBuf) {
		LOG(TegraPipeline, Error)
			<< "Request has no buffer for the output stream";
		return -ENOENT;
	}

	/* Record metadata tracking for this frame. */
	auto [it, inserted] = data->frameInfos_.try_emplace(
		request->sequence(),
		request, /*metadataRequired=*/true);
	if (!inserted) {
		LOG(TegraPipeline, Error)
			<< "Duplicate sequence number " << request->sequence();
		return -EINVAL;
	}

	/* Forward any per-request controls to the IPA. */
	data->swIsp_->queueRequest(request->sequence(),
				   request->controls());

	/* Enqueue the request output for the next raw-frame arrival. */
	data->pendingRequests_.push({ request, outBuf });

	return 0;
}

/* ---------------------------------------------------------------------- */
/*  match                                                                  */
/* ---------------------------------------------------------------------- */

/*
 * Given a camera sensor entity, walk downstream through the media graph to
 * find the first CSI bridge (MEDIA_ENT_F_VID_IF_BRIDGE) and then the VI
 * capture node (MEDIA_ENT_F_IO_V4L).
 *
 * Returns true when both entities are found.
 */
bool PipelineHandlerTegra::findPipelineEntities(
	MediaEntity *sensorEntity,
	MediaEntity **csiEntity,
	MediaEntity **viEntity)
{
	*csiEntity = nullptr;
	*viEntity = nullptr;

	for (MediaPad *pad : sensorEntity->pads()) {
		if (!(pad->flags() & MEDIA_PAD_FL_SOURCE))
			continue;

		for (MediaLink *link : pad->links()) {
			MediaEntity *downstream = link->sink()->entity();

			if (downstream->function() ==
			    MEDIA_ENT_F_VID_IF_BRIDGE) {
				*csiEntity = downstream;

				/* Follow one more hop to find the VI node. */
				for (MediaPad *csiPad : downstream->pads()) {
					if (!(csiPad->flags() &
					      MEDIA_PAD_FL_SOURCE))
						continue;

					for (MediaLink *csiLink :
					     csiPad->links()) {
						MediaEntity *vi =
							csiLink->sink()
								->entity();
						if (vi->function() ==
						    MEDIA_ENT_F_IO_V4L) {
							*viEntity = vi;
							return true;
						}
					}
				}
			}
		}
	}

	return false;
}

bool PipelineHandlerTegra::createCamera(
	std::shared_ptr<MediaDevice> media,
	MediaEntity *sensorEntity)
{
	MediaEntity *csiEntity = nullptr;
	MediaEntity *viEntity = nullptr;

	if (!findPipelineEntities(sensorEntity, &csiEntity, &viEntity)) {
		LOG(TegraPipeline, Debug)
			<< "No complete sensor→CSI→VI path from "
			<< sensorEntity->name();
		return false;
	}

	auto data =
		std::make_unique<TegraCameraData>(this, media);

	/* Create the camera sensor. */
	data->sensor_ =
		CameraSensorFactoryBase::create(sensorEntity);
	if (!data->sensor_) {
		LOG(TegraPipeline, Error)
			<< "Failed to create sensor for "
			<< sensorEntity->name();
		return false;
	}

	/* Open the CSI bridge subdev. */
	data->csi_ = std::make_unique<V4L2Subdevice>(csiEntity);

	/* Open the VI video device. */
	data->video_ =
		std::make_unique<V4L2VideoDevice>(viEntity);
	data->videoEntity_ = viEntity;

	if (data->init()) {
		LOG(TegraPipeline, Error)
			<< "Failed to initialise camera data for "
			<< sensorEntity->name();
		return false;
	}

	/* Assemble the camera and register it. */
	std::set<Stream *> streams{ &data->stream_ };
	const std::string id = data->sensor_->id();

	std::shared_ptr<Camera> camera =
		Camera::create(std::move(data), id, streams);
	registerCamera(std::move(camera));

	LOG(TegraPipeline, Info)
		<< "Registered Tegra camera: " << id
		<< " (" << sensorEntity->name() << " → "
		<< csiEntity->name() << " → "
		<< viEntity->name() << ")";

	return true;
}

bool PipelineHandlerTegra::match(DeviceEnumerator *enumerator)
{
	/*
	 * Try both known Tegra VI driver names.
	 *   "tegra-vi"    – Tegra 210 / 186 / 194  (Nano / TX2 / Xavier)
	 *   "tegra234-vi" – Tegra 234              (Orin)
	 */
	static const char *const kDriverNames[] = {
		"tegra-vi",
		"tegra234-vi",
	};

	bool matched = false;

	for (const char *driverName : kDriverNames) {
		DeviceMatch dm(driverName);
		std::shared_ptr<MediaDevice> media =
			acquireMediaDevice(enumerator, dm);
		if (!media)
			continue;

		LOG(TegraPipeline, Debug)
			<< "Found Tegra VI media device ("
			<< driverName << ")";

		/*
		 * Enumerate all camera sensor entities and attempt to build a
		 * camera for each one.
		 */
		for (MediaEntity *entity : media->entities()) {
			if (entity->function() != MEDIA_ENT_F_CAM_SENSOR)
				continue;

			if (createCamera(media, entity))
				matched = true;
		}
	}

	return matched;
}

REGISTER_PIPELINE_HANDLER(PipelineHandlerTegra, "tegra")

} /* namespace libcamera */
