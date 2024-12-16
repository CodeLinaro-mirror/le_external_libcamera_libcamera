/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * Copyright (C) 2019, Raspberry Pi Ltd
 * Copyright (C) 2024, Paul Elder <paul.elder@ideasonboard.com>
 *
 * Helper class that implements lux estimation
 */

#pragma once

#include <algorithm>
#include <tuple>
#include <vector>

#include <libcamera/base/utils.h>

#include "libcamera/internal/yaml_parser.h"

#include "histogram.h"

namespace libcamera {

namespace ipa {

class Lux
{
public:
	Lux() = default;
	~Lux() = default;

	void setBinSize(unsigned int binSize);
	int readYaml(const YamlObject &tuningData);
	double estimateLux(utils::Duration exposureTime,
			   double aGain, double dGain,
			   const Histogram &yHist) const;

private:
	unsigned int binSize_;
	utils::Duration referenceExposureTime_;
	double referenceAnalogueGain_;
	double referenceDigitalGain_;
	double referenceY_;
	double referenceLux_;
};

} /* namespace ipa */

} /* namespace libcamera */
