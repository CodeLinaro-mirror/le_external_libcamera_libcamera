/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * Copyright (C) 2026 Ideas on Board Oy
 *
 * Base classes and types for LSC algorithms implementations
 */

#pragma once

#include <map>
#include <string>
#include <vector>

#include <libcamera/geometry.h>

#include "libcamera/internal/value_node.h"

namespace libcamera {

namespace ipa {

namespace lsc {

template<typename T>
class Components : public std::map<std::string, std::vector<T>>
{
};

template<typename T>
class ComponentsMap : public std::map<unsigned int, Components<T>>
{
};

} /* namespace lsc */

struct LscDescriptor {
	std::vector<std::string> keys;
	unsigned int numHCells;
	unsigned int numVCells;
	Size sensorSize;
};

template<typename T, typename U>
class LscImplementation
{
public:
	virtual ~LscImplementation() = default;

	virtual int parseLscData(const ValueNode &tuningData,
				 const LscDescriptor &descriptor) = 0;

	virtual lsc::ComponentsMap<T> resampleLscData(const Rectangle &cropRectangle,
						      const std::vector<double> &xPos,
						      const std::vector<double> &yPos) = 0;
};

} /* namespace ipa */

} /* namespace libcamera */
