/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * Copyright (C) 2026 Ideas on Board Oy
 */

#pragma once


#include <libcamera/base/utils.h>

#include <libcamera/controls.h>
#include <libcamera/geometry.h>

#include <libcamera/ipa/core_ipa_interface.h>

#include "camera_sensor_helper.h"

namespace libcamera {

namespace ipa {

class AgcAlgorithm
{
public:
	struct Session {
		utils::Duration minExposureTime;
		utils::Duration maxExposureTime;
		double minAnalogueGain;
		double maxAnalogueGain;
		utils::Duration minFrameDuration;
		utils::Duration maxFrameDuration;

		utils::Duration lineDuration;

		struct {
			Size outputSize;
		} sensor;

		bool autoAllowed;
	};

	struct ActiveState {
		struct {
			uint32_t exposure;
			double gain;
		} manual;
		struct {
			uint32_t exposure;
			double gain;
		} automatic;

		bool autoExposureEnabled;
		bool autoGainEnabled;
		utils::Duration minFrameDuration;
		utils::Duration maxFrameDuration;
	};

	struct FrameContext {
		uint32_t exposure;
		double gain;
		uint32_t vblank;
		bool autoExposureEnabled;
		bool autoGainEnabled;
		utils::Duration minFrameDuration;
		utils::Duration maxFrameDuration;
		utils::Duration frameDuration;
		bool autoExposureModeChange;
		bool autoGainModeChange;
	};

	struct ConfigurationParams {
		const CameraSensorHelper *sensor;
		const IPACameraSensorInfo &sensorInfo;
		const ControlInfoMap &sensorControls;
		ControlInfoMap::Map &ctrlMap;
		bool autoAllowed = true;
	};

protected:
	int configure(Session &session, ActiveState &state, const ConfigurationParams &config);

	void queueRequest(const Session &session, ActiveState &state,
			  FrameContext &frameContext, const ControlList &controls);

	void prepare(ActiveState &state, FrameContext &frameContext);

	struct Limits {
		std::pair<utils::Duration, utils::Duration> exposure;
		std::pair<double, double> gain;
	};

	[[nodiscard]] Limits calculateLimits(const Session &session, const FrameContext &frameContext);

	void process(const Session& session, FrameContext& frameContext,
		     utils::Duration newExposureTime, ControlList &metadata);
};

} /* namespace ipa */

} /* namespace libcamera */
