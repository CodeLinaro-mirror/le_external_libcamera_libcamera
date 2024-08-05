/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * Copyright (C) 2023, Google Inc.
 *
 * virtual.cpp - Pipeline handler for virtual cameras
 */

#include "virtual.h"

#include <libcamera/base/log.h>

#include <libcamera/camera.h>
#include <libcamera/control_ids.h>
#include <libcamera/controls.h>
#include <libcamera/formats.h>
#include <libcamera/property_ids.h>

#include "libcamera/internal/camera.h"
#include "libcamera/internal/formats.h"
#include "libcamera/internal/pipeline_handler.h"

namespace libcamera {

LOG_DEFINE_CATEGORY(Virtual)

namespace {

uint64_t currentTimestamp()
{
	struct timespec ts;
	if (clock_gettime(CLOCK_MONOTONIC, &ts) < 0) {
		LOG(Virtual, Error) << "Get clock time fails";
		return 0;
	}

	return ts.tv_sec * 1'000'000'000LL + ts.tv_nsec;
}

} // namespace

VirtualCameraConfiguration::VirtualCameraConfiguration(VirtualCameraData *data)
	: CameraConfiguration(), data_(data)
{
}

CameraConfiguration::Status VirtualCameraConfiguration::validate()
{
	Status status = Valid;

	if (config_.empty()) {
		LOG(Virtual, Error) << "Empty config";
		return Invalid;
	}

	/* Currently only one stream is supported */
	if (config_.size() > 1) {
		config_.resize(1);
		status = Adjusted;
	}

	Size maxSize;
	for (const auto &resolution : data_->supportedResolutions_)
		maxSize = std::max(maxSize, resolution.size);

	for (StreamConfiguration &cfg : config_) {
		bool found = false;
		for (const auto &resolution : data_->supportedResolutions_) {
			if (resolution.size.width == cfg.size.width &&
			    resolution.size.height == cfg.size.height) {
				found = true;
				break;
			}
		}

		if (!found) {
			cfg.size = maxSize;
			status = Adjusted;
		}

		const PixelFormatInfo &info = PixelFormatInfo::info(cfg.pixelFormat);
		cfg.stride = info.stride(cfg.size.width, 0, 1);
		cfg.frameSize = info.frameSize(cfg.size, 1);

		cfg.setStream(const_cast<Stream *>(&data_->stream_));

		cfg.bufferCount = VirtualCameraConfiguration::kBufferCount;
	}

	return status;
}

PipelineHandlerVirtual::PipelineHandlerVirtual(CameraManager *manager)
	: PipelineHandler(manager)
{
}

std::unique_ptr<CameraConfiguration>
PipelineHandlerVirtual::generateConfiguration(Camera *camera,
					      Span<const StreamRole> roles)
{
	VirtualCameraData *data = cameraData(camera);
	auto config =
		std::make_unique<VirtualCameraConfiguration>(data);

	if (roles.empty())
		return config;

	Size minSize, sensorResolution;
	for (const auto &resolution : data->supportedResolutions_) {
		if (minSize.isNull() || minSize > resolution.size)
			minSize = resolution.size;

		sensorResolution = std::max(sensorResolution, resolution.size);
	}

	for (const StreamRole role : roles) {
		std::map<PixelFormat, std::vector<SizeRange>> streamFormats;
		unsigned int bufferCount;
		PixelFormat pixelFormat;

		switch (role) {
		case StreamRole::StillCapture:
			pixelFormat = formats::NV12;
			bufferCount = VirtualCameraConfiguration::kBufferCount;
			streamFormats[pixelFormat] = { { minSize, sensorResolution } };

			break;

		case StreamRole::Raw: {
			/* \todo check */
			pixelFormat = formats::SBGGR10;
			bufferCount = VirtualCameraConfiguration::kBufferCount;
			streamFormats[pixelFormat] = { { minSize, sensorResolution } };

			break;
		}

		case StreamRole::Viewfinder:
		case StreamRole::VideoRecording: {
			pixelFormat = formats::NV12;
			bufferCount = VirtualCameraConfiguration::kBufferCount;
			streamFormats[pixelFormat] = { { minSize, sensorResolution } };

			break;
		}

		default:
			LOG(Virtual, Error)
				<< "Requested stream role not supported: " << role;
			config.reset();
			return config;
		}

		StreamFormats formats(streamFormats);
		StreamConfiguration cfg(formats);
		cfg.size = sensorResolution;
		cfg.pixelFormat = pixelFormat;
		cfg.bufferCount = bufferCount;
		config->addConfiguration(cfg);
	}

	if (config->validate() == CameraConfiguration::Invalid)
		config.reset();

	return config;
}

int PipelineHandlerVirtual::configure(
	[[maybe_unused]] Camera *camera,
	[[maybe_unused]] CameraConfiguration *config)
{
	// Nothing to be done.
	return 0;
}

int PipelineHandlerVirtual::exportFrameBuffers(
	[[maybe_unused]] Camera *camera,
	Stream *stream,
	std::vector<std::unique_ptr<FrameBuffer>> *buffers)
{
	if (!dmaBufAllocator_.isValid())
		return -ENOBUFS;

	const StreamConfiguration &config = stream->configuration();

	auto info = PixelFormatInfo::info(config.pixelFormat);

	std::vector<std::size_t> planeSizes;
	for (size_t i = 0; i < info.planes.size(); ++i)
		planeSizes.push_back(info.planeSize(config.size, i));

	return dmaBufAllocator_.exportBuffers(config.bufferCount, planeSizes, buffers);
}

int PipelineHandlerVirtual::start([[maybe_unused]] Camera *camera,
				  [[maybe_unused]] const ControlList *controls)
{
	/* \todo Start reading the virtual video if any. */
	return 0;
}

void PipelineHandlerVirtual::stopDevice([[maybe_unused]] Camera *camera)
{
	/* \todo Reset the virtual video if any. */
}

int PipelineHandlerVirtual::queueRequestDevice([[maybe_unused]] Camera *camera,
					       Request *request)
{
	/* \todo Read from the virtual video if any. */
	for (auto it : request->buffers())
		completeBuffer(request, it.second);

	request->metadata().set(controls::SensorTimestamp, currentTimestamp());
	completeRequest(request);

	return 0;
}

bool PipelineHandlerVirtual::match([[maybe_unused]] DeviceEnumerator *enumerator)
{
	/* \todo Add virtual cameras according to a config file. */

	std::unique_ptr<VirtualCameraData> data = std::make_unique<VirtualCameraData>(this);

	data->supportedResolutions_.resize(2);
	data->supportedResolutions_[0] = { .size = Size(1920, 1080), .frame_rates = { 30 } };
	data->supportedResolutions_[1] = { .size = Size(1280, 720), .frame_rates = { 30, 60 } };

	data->properties_.set(properties::Location, properties::CameraLocationFront);
	data->properties_.set(properties::Model, "Virtual Video Device");
	data->properties_.set(properties::PixelArrayActiveAreas, { Rectangle(Size(1920, 1080)) });

	/* \todo Set FrameDurationLimits based on config. */
	ControlInfoMap::Map controls;
	int64_t min_frame_duration = 30, max_frame_duration = 60;
	controls[&controls::FrameDurationLimits] = ControlInfo(min_frame_duration, max_frame_duration);
	data->controlInfo_ = ControlInfoMap(std::move(controls), controls::controls);

	/* Create and register the camera. */
	std::set<Stream *> streams{ &data->stream_ };
	const std::string id = "Virtual0";
	std::shared_ptr<Camera> camera = Camera::create(std::move(data), id, streams);
	registerCamera(std::move(camera));

	return false; // Prevent infinite loops for now
}

REGISTER_PIPELINE_HANDLER(PipelineHandlerVirtual, "virtual")

} /* namespace libcamera */
