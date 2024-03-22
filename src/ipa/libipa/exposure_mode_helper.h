/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * Copyright (C) 2024, Paul Elder <paul.elder@ideasonboard.com>
 *
 * exposure_mode_helper.h - Helper class that performs computations relating to exposure
 */

#pragma once

#include <algorithm>
#include <tuple>
#include <vector>

#include <libcamera/base/utils.h>

namespace libcamera {

namespace ipa {

class ExposureModeHelper
{
public:
	ExposureModeHelper();
	~ExposureModeHelper();

	int init(std::vector<utils::Duration> &shutters, std::vector<double> &gains);
	void configure(utils::Duration minShutter, utils::Duration maxShutter,
		       double minGain, double maxGain);

	std::tuple<utils::Duration, double, double> splitExposure(utils::Duration exposure);
	std::tuple<utils::Duration, double, double> splitExposure(utils::Duration exposure,
								  utils::Duration fixedShutter);
	std::tuple<utils::Duration, double, double> splitExposure(utils::Duration exposure,
								  double fixedGain);

	utils::Duration minShutter() { return minShutter_; };
	utils::Duration maxShutter() { return maxShutter_; };
	double minGain() { return minGain_; };
	double maxGain() { return maxGain_; };

private:
	utils::Duration clampShutter(utils::Duration shutter);
	double clampGain(double gain);

	std::tuple<utils::Duration, double, double>
	splitExposure(utils::Duration exposure,
		      utils::Duration shutter, bool shutterFixed,
		      double gain, bool gainFixed);

	std::vector<utils::Duration> shutters_;
	std::vector<double> gains_;

	utils::Duration minShutter_;
	utils::Duration maxShutter_;
	double minGain_;
	double maxGain_;
};

} /* namespace ipa */

} /* namespace libcamera */
