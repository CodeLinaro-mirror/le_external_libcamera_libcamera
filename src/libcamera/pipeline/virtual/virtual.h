/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * Copyright (C) 2024, Google Inc.
 *
 * virtual.h - Pipeline handler for virtual cameras
 */

#pragma once

#include <string>
#include <variant>
#include <vector>

#include <libcamera/base/file.h>

#include "libcamera/internal/camera.h"
#include "libcamera/internal/pipeline_handler.h"

#include "frame_generator.h"
#include "image_frame_generator.h"
#include "test_pattern_generator.h"

namespace libcamera {

using VirtualFrame = std::variant<TestPattern, ImageFrames>;

class VirtualCameraData : public Camera::Private
{
public:
	const static unsigned int kMaxStream = 1;

	struct Resolution {
		Size size;
		std::vector<int> frameRates;
	};
	struct StreamConfig {
		Stream stream;
		std::unique_ptr<FrameGenerator> frameGenerator;
	};
	/* The config file is parsed to the Configuration struct */
	struct Configuration {
		std::string id;
		std::vector<Resolution> resolutions;
		VirtualFrame frame;

		Size maxResolutionSize;
		Size minResolutionSize;
	};

	VirtualCameraData(PipelineHandler *pipe,
			  std::vector<Resolution> supportedResolutions);

	~VirtualCameraData() = default;

	Configuration config_;

	std::vector<StreamConfig> streamConfigs_;
};

} /* namespace libcamera */
