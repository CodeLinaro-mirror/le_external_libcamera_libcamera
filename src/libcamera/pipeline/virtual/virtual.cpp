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
#include "libcamera/internal/yaml_parser.h"

#include "frame_generator.h"
#include "parser.h"

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

int PipelineHandlerVirtual::start(Camera *camera,
				  [[maybe_unused]] const ControlList *controls)
{
	/* \todo Start reading the virtual video if any. */
	VirtualCameraData *data = cameraData(camera);

	data->frameGenerator_->configure(data->stream_.configuration().size);

	return 0;
}

void PipelineHandlerVirtual::stopDevice([[maybe_unused]] Camera *camera)
{
	/* \todo Reset the virtual video if any. */
}

int PipelineHandlerVirtual::queueRequestDevice([[maybe_unused]] Camera *camera,
					       Request *request)
{
	VirtualCameraData *data = cameraData(camera);

	/* \todo Read from the virtual video if any. */
	for (auto const &[stream, buffer] : request->buffers()) {
		/* map buffer and fill test patterns */
		data->frameGenerator_->generateFrame(stream->configuration().size, buffer);
		completeBuffer(request, buffer);
	}

	request->metadata().set(controls::SensorTimestamp, currentTimestamp());
	completeRequest(request);

	return 0;
}

bool PipelineHandlerVirtual::match([[maybe_unused]] DeviceEnumerator *enumerator)
{
	File file(configurationFile("virtual", "virtual.yaml"));
	bool isOpen = file.open(File::OpenModeFlag::ReadOnly);
	if (!isOpen) {
		LOG(Virtual, Error) << "Failed to open config file: " << file.fileName();
		return false;
	}

	Parser parser;
	auto configData = parser.parseConfigFile(file, this);
	if (configData.size() == 0) {
		LOG(Virtual, Error) << "Failed to parse any cameras from the config file: "
				    << file.fileName();
		return false;
	}

	/* Configure and register cameras with configData */
	for (auto &data : configData) {
		std::set<Stream *> streams{ &data->stream_ };
		std::string id = data->id_;
		std::shared_ptr<Camera> camera = Camera::create(std::move(data), id, streams);

		initFrameGenerator(camera.get());

		registerCamera(std::move(camera));
	}

	return false; // Prevent infinite loops for now
}

void PipelineHandlerVirtual::initFrameGenerator(Camera *camera)
{
	auto data = cameraData(camera);
	if (data->testPattern_ == TestPattern::DiagonalLines) {
		data->frameGenerator_ = DiagonalLinesGenerator::create();
	} else {
		data->frameGenerator_ = ColorBarsGenerator::create();
	}
}

REGISTER_PIPELINE_HANDLER(PipelineHandlerVirtual, "virtual")

} /* namespace libcamera */
