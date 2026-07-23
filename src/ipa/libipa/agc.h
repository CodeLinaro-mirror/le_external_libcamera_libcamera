/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * Copyright (C) 2026 Ideas on Board Oy
 *
 * AGC-related functionality
 */

#pragma once

#include <optional>
#include <utility>

#include <linux/v4l2-controls.h>

#include <libcamera/control_ids.h>
#include <libcamera/controls.h>

#include <libcamera/ipa/core_ipa_interface.h>

#include "agc_mean_luminance.h"
#include "camera_sensor_helper.h"
#include "histogram.h"

namespace libcamera {

namespace ipa {

namespace agc {

[[nodiscard]]
inline std::pair<uint32_t, double>
extractControls(const ControlList &controls, const CameraSensorHelper *sensor)
{
	auto exposure = controls.get(V4L2_CID_EXPOSURE).get<int32_t>();
	auto gainCode = controls.get(V4L2_CID_ANALOGUE_GAIN).get<int32_t>();

	return {
		uint32_t(exposure),
		sensor ? sensor->gain(gainCode) : gainCode,
	};
}

inline void
prepareControls(ControlList &controls, const CameraSensorHelper *sensor,
		int32_t exposure, double gain)
{
	controls.set(V4L2_CID_EXPOSURE, exposure);
	controls.set(V4L2_CID_ANALOGUE_GAIN, int32_t(sensor ? sensor->gainCode(gain) : gain));
}

struct Session {
	uint32_t minExposure;
	uint32_t maxExposure;
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
		double quantizationGain;
		double yTarget;
	} automatic;

	bool autoExposureEnabled;
	bool autoGainEnabled;
	double exposureValue;
	controls::AeConstraintModeEnum constraintMode;
	controls::AeExposureModeEnum exposureMode;
	utils::Duration minFrameDuration;
	utils::Duration maxFrameDuration;
};

struct FrameContext {
	uint32_t exposure;
	double gain;
	double quantizationGain;
	double exposureValue;
	double yTarget;
	uint32_t vblank;
	bool autoExposureEnabled;
	bool autoGainEnabled;
	controls::AeConstraintModeEnum constraintMode;
	controls::AeExposureModeEnum exposureMode;
	utils::Duration minFrameDuration;
	utils::Duration maxFrameDuration;
	utils::Duration frameDuration;
	bool autoExposureModeChange;
	bool autoGainModeChange;
};

} /* namespace agc */

class AgcAlgorithm
{
public:
	struct ConfigurationParams {
		const CameraSensorHelper *sensor;
		const IPACameraSensorInfo &sensorInfo;
		const ControlInfoMap &sensorControls;
		ControlInfoMap::Map &ctrlMap;
		bool autoAllowed = true;
	};

	int init(const ValueNode &tuningData);

	int configure(agc::Session &session, agc::ActiveState &state, const ConfigurationParams &config);

	void queueRequest(const agc::Session &session, agc::ActiveState &state,
			  agc::FrameContext &frameContext, const ControlList &controls);

	void prepare(agc::ActiveState &state, agc::FrameContext &frameContext);

	struct ProcessParams {
		const AgcMeanLuminance::Traits &traits;
		const Histogram &yHist;
		uint32_t exposure;
		double gain;
		std::vector<AgcMeanLuminance::AgcConstraint> &&additionalConstraints = {};
		double lux = 0;
	};

	void process(const agc::Session &session, agc::ActiveState &state, agc::FrameContext &frameContext,
		     std::optional<ProcessParams> &&params, ControlList &metadata);

private:
	AgcMeanLuminance impl_;
};

} /* namespace ipa */

} /* namespace libcamera */
