/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * Copyright (C) 2024, Ideas On Board
 *
 * Mali-C55 IPA Context
 */

#pragma once

#include <libcamera/base/utils.h>
#include <libcamera/controls.h>

#include <libcamera/ipa/core_ipa_interface.h>

#include "libcamera/internal/bayer_format.h"

#include <libipa/camera_sensor_helper.h>
#include <libipa/fc_queue.h>

namespace libcamera {

namespace ipa::mali_c55 {

struct IPASessionConfiguration {
	struct {
		uint32_t defaultExposure;
	} agc;

	struct {
		BayerFormat::Order bayerOrder;
		utils::Duration lineDuration;
		uint32_t blackLevel;
		utils::Duration minShutterSpeed;
		utils::Duration minFrameDuration;
		utils::Duration maxFrameDuration;
		double minAnalogueGain;
		double maxAnalogueGain;
	} sensor;
};

struct IPAActiveState {
	struct {
		struct {
			uint32_t exposure;
			double sensorGain;
			double ispGain;
		} automatic;
		struct {
			uint32_t exposure;
			double sensorGain;
			double ispGain;
		} manual;
		bool autoEnabled;
		uint32_t constraintMode;
		uint32_t exposureMode;
		uint32_t temperatureK;
		utils::Duration minFrameDuration;
		utils::Duration maxFrameDuration;
	} agc;

	struct {
		double rGain;
		double bGain;
	} awb;
};

struct IPAFrameContext : public FrameContext {
	struct {
		uint32_t exposure;
		double sensorGain;
		double ispGain;
		uint32_t vblank;
		utils::Duration minFrameDuration;
		utils::Duration maxFrameDuration;
	} agc;

	struct {
		double rGain;
		double bGain;
	} awb;
};

struct IPAContext {
	IPAContext(unsigned int frameContextSize)
		: frameContexts(frameContextSize)
	{
	}

	IPACameraSensorInfo sensorInfo;
	IPASessionConfiguration configuration;
	IPAActiveState activeState;

	FCQueue<IPAFrameContext> frameContexts;

	ControlInfoMap::Map ctrlMap;

	/* Interface to the Camera Helper */
	std::unique_ptr<CameraSensorHelper> camHelper;
};

} /* namespace ipa::mali_c55 */

} /* namespace libcamera*/
