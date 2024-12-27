/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * Copyright (C) 2024, Amlogic Inc.
 *
 * C3ISP IPA Context
 */

#pragma once

#include <memory>

#include <linux/c3-isp-config.h>

#include <libcamera/base/utils.h>

#include <libcamera/control_ids.h>
#include <libcamera/controls.h>
#include <libcamera/geometry.h>
#include <libcamera/ipa/core_ipa_interface.h>

#include <libipa/camera_sensor_helper.h>
#include <libipa/fc_queue.h>

namespace libcamera {

namespace ipa::c3isp {

struct IPASessionConfiguration {
	struct {
		utils::Duration minShutterSpeed;
		utils::Duration maxShutterSpeed;
		double minAnalogueGain;
		double maxAnalogueGain;

		int32_t defVBlank;
		utils::Duration lineDuration;
		Size size;
	} sensor;
};

struct IPAActiveState {
	struct {
		uint32_t exposure;
		double gain;
		uint32_t constraintMode;
		uint32_t exposureMode;
	} agc;

	struct {
		struct {
			struct {
				double red;
				double green;
				double blue;
			} manual;
			struct {
				double red;
				double green;
				double blue;
			} automatic;
		} gains;

		unsigned int temperatureK;
		bool autoEnabled;
	} awb;
};

struct IPAFrameContext : public FrameContext {
	struct {
		uint32_t exposure;
		double gain;
		bool autoEnabled;
		controls::AeConstraintModeEnum constraintMode;
		controls::AeExposureModeEnum exposureMode;
		controls::AeMeteringModeEnum meteringMode;
		utils::Duration maxFrameDuration;
		bool updateMetering;
	} agc;

	struct {
		struct {
			double red;
			double green;
			double blue;
		} gains;

		bool autoEnabled;
	} awb;

	struct {
		uint32_t exposure;
		double gain;
	} sensor;
};

struct IPAContext {
	IPASessionConfiguration configuration;
	IPAActiveState activeState;

	FCQueue<IPAFrameContext> frameContexts;

	ControlInfoMap::Map ctrlMap;

	/* Interface to the Camera Helper */
	std::unique_ptr<CameraSensorHelper> camHelper;
};

} /* namespace ipa::c3isp */

} /* namespace libcamera*/
