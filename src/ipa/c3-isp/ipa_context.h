/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * Copyright (C) 2024, Amlogic Inc.
 *
 * C3ISP IPA Context
 */

#pragma once

#include <libcamera/base/utils.h>
#include <libcamera/controls.h>

#include <libipa/fc_queue.h>

namespace libcamera {

namespace ipa::c3isp {

struct IPASessionConfiguration {
	struct {
		utils::Duration minShutterSpeed;
		utils::Duration maxShutterSpeed;
		uint32_t defaultExposure;
		double minAnalogueGain;
		double maxAnalogueGain;
	} agc;

	struct {
		utils::Duration lineDuration;
		uint32_t blackLevel;
		Size size;
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
	} agc;

	struct {
		double rGain;
		double bGain;
		uint32_t temperatureK;
	} awb;
};

struct IPAFrameContext : public FrameContext {
	struct {
		uint32_t exposure;
		double sensorGain;
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

	IPASessionConfiguration configuration;
	IPAActiveState activeState;

	FCQueue<IPAFrameContext> frameContexts;

	ControlInfoMap::Map ctrlMap;
};

} /* namespace ipa::c3isp */

} /* namespace libcamera*/
