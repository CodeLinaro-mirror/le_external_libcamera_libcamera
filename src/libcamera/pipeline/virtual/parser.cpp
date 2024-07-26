/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * Copyright (C) 2023, Google Inc.
 *
 * parser.cpp - Virtual cameras helper to parse config file
 */

#include "parser.h"

#include <memory>
#include <utility>

#include <libcamera/base/log.h>

#include <libcamera/control_ids.h>
#include <libcamera/property_ids.h>

#include "libcamera/internal/pipeline_handler.h"
#include "libcamera/internal/yaml_parser.h"

#include "common_functions.h"
#include "virtual.h"

namespace libcamera {

LOG_DECLARE_CATEGORY(Virtual)

std::vector<std::unique_ptr<VirtualCameraData>> Parser::parseConfigFile(
	File &file, PipelineHandler *pipe)
{
	std::vector<std::unique_ptr<VirtualCameraData>> configurations;

	std::unique_ptr<YamlObject> cameras = YamlParser::parse(file);
	if (!cameras) {
		LOG(Virtual, Error) << "Failed to pass config file.";
		return configurations;
	}

	if (!cameras->isDictionary()) {
		LOG(Virtual, Error) << "Config file is not a dictionary at the top level.";
		return configurations;
	}

	/* Look into the configuration of each camera */
	for (const auto &[cameraId, cameraConfigData] : cameras->asDict()) {
		std::unique_ptr<VirtualCameraData> data =
			parseCameraConfigData(cameraConfigData, pipe);
		/* Parse configData to data*/
		if (!data) {
			/* Skip the camera if it has invalid config */
			LOG(Virtual, Error) << "Failed to parse config of the camera: "
					    << cameraId;
			continue;
		}

		data->config_.id = cameraId;
		ControlInfoMap::Map controls;
		/* todo: Check which resolution's frame rate to be reported */
		controls[&controls::FrameDurationLimits] =
			ControlInfo(int64_t(1000 / data->config_.resolutions[0].frameRates[1]),
				    int64_t(1000 / data->config_.resolutions[0].frameRates[0]));
		data->controlInfo_ = ControlInfoMap(std::move(controls), controls::controls);
		configurations.push_back(std::move(data));
	}
	return configurations;
}

std::unique_ptr<VirtualCameraData> Parser::parseCameraConfigData(
	const YamlObject &cameraConfigData, PipelineHandler *pipe)
{
	std::unique_ptr<VirtualCameraData> data = std::make_unique<VirtualCameraData>(pipe);

	if (parseSupportedFormats(cameraConfigData, data.get()))
		return nullptr;

	if (parseFrame(cameraConfigData, data.get()))
		return nullptr;

	if (parseLocation(cameraConfigData, data.get()))
		return nullptr;

	if (parseModel(cameraConfigData, data.get()))
		return nullptr;

	return data;
}

int Parser::parseSupportedFormats(
	const YamlObject &cameraConfigData, VirtualCameraData *data)
{
	Size activeResolution{ 0, 0 };
	if (cameraConfigData.contains("supported_formats")) {
		const YamlObject &supportedResolutions = cameraConfigData["supported_formats"];

		for (const YamlObject &supportedResolution : supportedResolutions.asList()) {
			unsigned int width = supportedResolution["width"].get<unsigned int>(1920);
			unsigned int height = supportedResolution["height"].get<unsigned int>(1080);
			if (width <= 0 || height <= 0) {
				LOG(Virtual, Error) << "Invalid width or/and height";
				return -EINVAL;
			}
			if (width % 2 != 0) {
				LOG(Virtual, Error) << "Invalid width: width needs to be even";
				return -EINVAL;
			}

			std::vector<int> frameRates;
			if (supportedResolution.contains("frame_rates")) {
				auto frameRatesList =
					supportedResolution["frame_rates"].getList<int>().value();
				if (frameRatesList.size() != 2) {
					LOG(Virtual, Error) << "frame_rates needs to be the two edge values of a range";
					return -EINVAL;
				}
				if (frameRatesList[0] > frameRatesList[1]) {
					LOG(Virtual, Error) << "frame_rates's first value(lower bound) is higher than the second value(upper bound)";
					return -EINVAL;
				}
				frameRates.push_back(frameRatesList[0]);
				frameRates.push_back(frameRatesList[1]);
			} else {
				frameRates.push_back(30);
				frameRates.push_back(60);
			}

			data->config_.resolutions.emplace_back(
				VirtualCameraData::Resolution{ Size{ width, height },
							       frameRates });

			activeResolution = std::max(activeResolution, Size{ width, height });
		}
	} else {
		data->config_.resolutions.emplace_back(
			VirtualCameraData::Resolution{ Size{ 1920, 1080 },
						       { 30, 60 } });
		activeResolution = Size(1920, 1080);
	}

	data->properties_.set(properties::PixelArrayActiveAreas,
			      { Rectangle(activeResolution) });

	return 0;
}

int Parser::parseFrame(
	const YamlObject &cameraConfigData, VirtualCameraData *data)
{
	const YamlObject &frames = cameraConfigData["frames"];
	/* When there is no frames provided in the config file, use color bar test pattern */
	if (frames.size() == 0) {
		data->config_.frame = TestPattern::ColorBars;
		return 0;
	}

	if (!frames.isDictionary()) {
		LOG(Virtual, Error) << "'frames' is not a dictionary.";
		return -EINVAL;
	}

	std::string path = frames["path"].get<std::string>().value();

	if (auto ext = getExtension(path); ext == ".jpg" || ext == ".jpeg") {
		ScaleMode scaleMode;
		if (parseScaleMode(frames, &scaleMode))
			return -EINVAL;
		data->config_.frame = ImageFrames{ path, scaleMode, std::nullopt };
	} else if (path.back() == '/') {
		ScaleMode scaleMode;
		if (parseScaleMode(frames, &scaleMode))
			return -EINVAL;
		data->config_.frame = ImageFrames{ path, scaleMode,
						   numberOfFilesInDirectory(path) };
	} else if (path == "bars" || path == "") {
		/* Default value is "bars" */
		data->config_.frame = TestPattern::ColorBars;
	} else if (path == "lines") {
		data->config_.frame = TestPattern::DiagonalLines;
	} else {
		LOG(Virtual, Error) << "Frame: " << path
				    << " is not supported";
		return -EINVAL;
	}
	return 0;
}

int Parser::parseScaleMode(
	const YamlObject &framesConfigData, ScaleMode *scaleMode)
{
	std::string mode = framesConfigData["scale_mode"].get<std::string>().value();

	/* Default value is fill */
	if (mode == "fill" || mode == "") {
		*scaleMode = ScaleMode::Fill;
	} else if (mode == "contain") {
		*scaleMode = ScaleMode::Contain;
	} else if (mode == "cover") {
		*scaleMode = ScaleMode::Cover;
	} else {
		LOG(Virtual, Error) << "scaleMode: " << mode
				    << " is not supported";
		return -EINVAL;
	}

	return 0;
}

int Parser::parseLocation(
	const YamlObject &cameraConfigData, VirtualCameraData *data)
{
	std::string location = cameraConfigData["location"].get<std::string>().value();

	/* Default value is properties::CameraLocationFront */
	if (location == "front" || location == "") {
		data->properties_.set(properties::Location,
				      properties::CameraLocationFront);
	} else if (location == "back") {
		data->properties_.set(properties::Location,
				      properties::CameraLocationBack);
	} else {
		LOG(Virtual, Error) << "location: " << location
				    << " is not supported";
		return -EINVAL;
	}

	return 0;
}

int Parser::parseModel(
	const YamlObject &cameraConfigData, VirtualCameraData *data)
{
	std::string model =
		cameraConfigData["model"].get<std::string>().value();

	/* Default value is "Unknown" */
	if (model == "")
		data->properties_.set(properties::Model, "Unknown");
	else
		data->properties_.set(properties::Model, model);

	return 0;
}

} // namespace libcamera
