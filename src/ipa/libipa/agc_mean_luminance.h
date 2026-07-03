/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * Copyright (C) 2024 Ideas on Board Oy
 *
 agc_mean_luminance.h - Base class for mean luminance AGC algorithms
 */

#pragma once

#include <map>
#include <memory>
#include <optional>
#include <tuple>
#include <vector>

#include <libcamera/base/utils.h>

#include <libcamera/control_ids.h>
#include <libcamera/controls.h>

#include <libcamera/ipa/core_ipa_interface.h>

#include "libcamera/internal/value_node.h"

#include "exposure_mode_helper.h"
#include "histogram.h"
#include "pwl.h"

namespace libcamera {

namespace ipa {

class AgcMeanLuminance
{
public:
	AgcMeanLuminance();
	~AgcMeanLuminance();

	struct AgcConstraint {
		enum class Bound {
			Lower = 0,
			Upper = 1
		};
		Bound bound;
		double qLo;
		double qHi;
		Pwl yTarget;
	};

	struct Traits {
		virtual ~Traits() = default;
		virtual double estimateLuminance(double gain) const = 0;
	};

	void configure(utils::Duration lineDuration, const CameraSensorHelper *sensorHelper);
	int parseTuningData(const ValueNode &tuningData);

	void setExposureCompensation(double gain)
	{
		exposureCompensation_ = gain;
	}

	void setLux(unsigned int lux)
	{
		lux_ = lux;
	}

	void setLimits(utils::Duration minExposureTime, utils::Duration maxExposureTime,
		       double minGain, double maxGain, std::vector<AgcConstraint> constraints);

	const std::map<int32_t, std::vector<AgcConstraint>> &constraintModes() const
	{
		return constraintModes_;
	}

	const std::map<int32_t, std::shared_ptr<ExposureModeHelper>> &exposureModeHelpers() const
	{
		return exposureModeHelpers_;
	}

	const ControlInfoMap::Map &controls() const
	{
		return controls_;
	}

	std::tuple<utils::Duration, double, double, double>
	calculateNewEv(uint32_t constraintModeIndex, uint32_t exposureModeIndex,
		       const Histogram &yHist, utils::Duration effectiveExposureValue,
		       const Traits &traits);

	double effectiveYTarget() const;

	void resetFrameCount()
	{
		frameCount_ = 0;
	}

private:
	int parseRelativeLuminanceTarget(const ValueNode &tuningData);
	int parseConstraint(const ValueNode &modeDict, int32_t id);
	int parseConstraintModes(const ValueNode &tuningData);
	int parseExposureModes(const ValueNode &tuningData);
	double estimateInitialGain(const Traits &traits) const;
	double constraintClampGain(uint32_t constraintModeIndex,
				   const Histogram &hist,
				   double gain);
	utils::Duration filterExposure(utils::Duration exposureValue);

	utils::Duration filteredExposure_;
	mutable bool luxWarningEnabled_;
	double exposureCompensation_;
	Pwl relativeLuminanceTarget_;
	uint64_t frameCount_;
	unsigned int lux_;

	std::vector<AgcConstraint> additionalConstraints_;
	std::map<int32_t, std::vector<AgcConstraint>> constraintModes_;
	std::map<int32_t, std::shared_ptr<ExposureModeHelper>> exposureModeHelpers_;
	ControlInfoMap::Map controls_;
};

class AgcMeanLuminanceAlgorithm
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

	struct ConfigurationParams {
		const CameraSensorHelper &sensor;
		const IPACameraSensorInfo &sensorInfo;
		const ControlInfoMap &sensorControls;
		ControlInfoMap::Map &ctrlMap;
		bool autoAllowed = true;
	};

	int init(const ValueNode &tuningData);

	int configure(Session &session, ActiveState &state, const ConfigurationParams &config);

	void queueRequest(const Session &session, ActiveState &state,
			  FrameContext &frameContext, const ControlList &controls);

	void prepare(ActiveState &state, FrameContext &frameContext);

	struct ProcessParams {
		const AgcMeanLuminance::Traits &traits;
		const Histogram &hist;
		uint32_t exposure;
		double gain;
		std::vector<AgcMeanLuminance::AgcConstraint> &&additionalConstraints = {};
		unsigned int lux = 0;
	};

	void process(const Session &session, ActiveState &state, FrameContext &frameContext,
		     std::optional<ProcessParams> &&params, ControlList &metadata);

private:
	AgcMeanLuminance impl_;
};

} /* namespace ipa */

} /* namespace libcamera */
