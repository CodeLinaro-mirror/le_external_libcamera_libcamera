/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * Copyright (C) 2024, Ideas On Board
 *
 * Mali-C55 IPA Context
 */

#pragma once

#include <libcamera/base/utils.h>
#include <libcamera/controls.h>

#include "libcamera/internal/bayer_format.h"

#include <libipa/agc.h>
#include <libipa/camera_sensor_helper.h>
#include <libipa/fc_queue.h>

#include "libipa/fixedpoint.h"

namespace libcamera {

namespace ipa::mali_c55 {

struct IPASessionConfiguration {
	agc::Session agc;

	struct {
		BayerFormat::Order bayerOrder;
		uint32_t blackLevel;
	} sensor;
};

struct IPAActiveState {
	struct Agc : agc::ActiveState {
		uint32_t temperatureK;
	} agc;

	struct {
		UQ<4, 8> rGain;
		UQ<4, 8> bGain;
	} awb;
};

struct IPAFrameContext : public FrameContext {
	agc::FrameContext agc;

	struct {
		uint32_t exposure;
		double gain;
	} sensor;

	struct {
		UQ<4, 8> rGain;
		UQ<4, 8> bGain;
	} awb;
};

struct IPAContext {
	IPAContext(unsigned int frameContextSize)
		: frameContexts(frameContextSize)
	{
	}

	IPASessionConfiguration configuration;
	IPACameraSensorInfo sensorInfo;
	IPAActiveState activeState;

	FCQueue<IPAFrameContext> frameContexts;

	ControlInfoMap sensorControls;

	std::unique_ptr<CameraSensorHelper> camHelper;

	ControlInfoMap::Map ctrlMap;
};

} /* namespace ipa::mali_c55 */

} /* namespace libcamera*/
