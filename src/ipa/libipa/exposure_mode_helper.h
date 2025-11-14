/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * Copyright (C) 2024, Paul Elder <paul.elder@ideasonboard.com>
 *
 * Helper class that performs computations relating to exposure
 */

#pragma once

#include <optional>
#include <tuple>
#include <utility>
#include <vector>

#include <libcamera/base/span.h>
#include <libcamera/base/utils.h>

#include "camera_sensor_helper.h"

namespace libcamera {

namespace ipa {

using namespace std::literals::chrono_literals;

class ExposureModeHelper
{
public:
	struct SensorConfiguration {
		utils::Duration lineDuration_;
		utils::Duration minExposureTime_;
		utils::Duration maxExposureTime_;
		utils::Duration minFrameDuration_;
		utils::Duration maxFrameDuration_;
		double minGain_;
		double maxGain_;
	};

	ExposureModeHelper(const Span<std::pair<utils::Duration, double>> stages);
	~ExposureModeHelper() = default;

	void configure(const SensorConfiguration &sensorConfig,
		       const CameraSensorHelper *sensorHelper);
	void setExposureLimits(std::optional<utils::Duration> shutterTime,
			       std::optional<double> gain,
			       utils::Duration maxFrameDuration);

	std::tuple<utils::Duration, double, double, double>
	splitExposure(utils::Duration exposure) const;

private:
	void setShutterLimits(std::optional<utils::Duration> shutterTime,
			      utils::Duration maxFrameDuration);
	void setGainLimits(std::optional<double> gain);

	utils::Duration clampExposureTime(utils::Duration exposureTime,
					  double *quantizationGain = nullptr) const;
	double clampGain(double gain, double *quantizationGain = nullptr) const;

	std::vector<utils::Duration> exposureTimes_;
	std::vector<double> gains_;

	SensorConfiguration sensor_;
	const CameraSensorHelper *sensorHelper_ = nullptr;

	/* Runtime parameters, used to split exposure. */
	utils::Duration minExposureTime_;
	utils::Duration maxExposureTime_;
	double minGain_;
	double maxGain_;
};

} /* namespace ipa */

} /* namespace libcamera */
