/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * Copyright (C) 2026 Ideas on Board Oy
 *
 * Base classes and types for LSC algorithm implementations
 */

#pragma once

#include <map>
#include <stdint.h>
#include <string>
#include <vector>

#include <libcamera/base/span.h>

#include <libcamera/geometry.h>

#include "libcamera/internal/value_node.h"

#include "interpolator.h"

namespace libcamera {

namespace ipa {

namespace lsc {

using Components = std::map<std::string, std::vector<float>>;
using ComponentsMap = std::map<unsigned int, Components>;

} /* namespace lsc */

struct LscDescriptor {
	std::vector<std::string> keys;
	unsigned int numHSamples;
	unsigned int numVSamples;
	Size sensorSize;
};

class LscImplementation
{
public:
	virtual ~LscImplementation() {}

	virtual int parseLscData(const ValueNode &tuningData,
				 const LscDescriptor &descriptor) = 0;

	virtual lsc::ComponentsMap
	sampleForCrop(const Rectangle &cropRectangle,
		      std::vector<double> xPos, std::vector<double> yPos) = 0;
};

} /* namespace ipa */

} /* namespace libcamera */
