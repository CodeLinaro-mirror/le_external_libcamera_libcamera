/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * Copyright (C) 2024 Ideas on Board Oy
 *
 agc.h - Base class for libipa-compliant AGC algorithms
 */

#pragma once

#include <tuple>
#include <vector>

#include <libcamera/controls.h>

#include "libcamera/internal/yaml_parser.h"

#include "exposure_mode_helper.h"
#include "histogram.h"

namespace libcamera {

namespace ipa {

class MeanLuminanceAgc
{
public:
	MeanLuminanceAgc();
	virtual ~MeanLuminanceAgc() = default;

	struct AgcConstraint {
		enum class Bound {
			LOWER = 0,
			UPPER = 1
		};
		Bound bound;
		double qLo;
		double qHi;
		double yTarget;
	};

	void parseRelativeLuminanceTarget(const YamlObject &tuningData);
	void parseConstraint(const YamlObject &modeDict, int32_t id);
	int parseConstraintModes(const YamlObject &tuningData);
	int parseExposureModes(const YamlObject &tuningData);

	std::map<int32_t, std::vector<AgcConstraint>> constraintModes()
	{
		return constraintModes_;
	}

	std::map<int32_t, std::shared_ptr<ExposureModeHelper>> exposureModeHelpers()
	{
		return exposureModeHelpers_;
	}

	ControlInfoMap::Map controls()
	{
		return controls_;
	}

	virtual double estimateLuminance(const double gain) = 0;
	double estimateInitialGain();
	double constraintClampGain(uint32_t constraintModeIndex,
				   const Histogram &hist,
				   double gain);
	utils::Duration filterExposure(utils::Duration exposureValue);
	std::tuple<utils::Duration, double, double>
	calculateNewEv(uint32_t constraintModeIndex, uint32_t exposureModeIndex,
		       const Histogram &yHist, utils::Duration effectiveExposureValue);
private:
	uint64_t frameCount_;
	utils::Duration filteredExposure_;
	double relativeLuminanceTarget_;

	std::map<int32_t, std::vector<AgcConstraint>> constraintModes_;
	std::map<int32_t, std::shared_ptr<ExposureModeHelper>> exposureModeHelpers_;
	ControlInfoMap::Map controls_;
};

}; /* namespace ipa */

}; /* namespace libcamera */
