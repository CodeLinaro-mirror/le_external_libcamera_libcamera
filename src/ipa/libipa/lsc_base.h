/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * Copyright (C) 2026 Ideas on Board Oy
 *
 * Base classes and types for LSC algorithms implementations
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

template<typename T>
class Components : public std::map<std::string, std::vector<T>>
{
};

template<typename T>
class ComponentsMap : public std::map<unsigned int, Components<T>>
{
};

} /* namespace lsc */

#ifndef __DOXYGEN__
template<typename T>
void interpolateVector(const std::vector<T> &a, const std::vector<T> &b,
		       std::vector<T> &dest, double lambda)
{
	ASSERT(a.size() == b.size());
	dest.resize(a.size());
	for (size_t i = 0; i < a.size(); i++)
		dest[i] = a[i] * (1.0 - lambda) + b[i] * lambda;
}

template<>
void Interpolator<lsc::Components<uint16_t>>::
	interpolate(const lsc::Components<uint16_t> &a,
		    const lsc::Components<uint16_t> &b,
		    lsc::Components<uint16_t> &dest,
		    double lambda);
#endif /* __DOXYGEN__ */

struct LscDescriptor {
	std::vector<std::string> keys;
	unsigned int numHCells;
	unsigned int numVCells;
	Size sensorSize;
};

template<typename U>
class LscImplementation
{
public:
	virtual ~LscImplementation() {}

	virtual int parseLscData(const ValueNode &tuningData,
				 const LscDescriptor &descriptor) = 0;

	virtual lsc::ComponentsMap<typename U::QuantizedType>
	sampleForCrop(const Rectangle &cropRectangle,
		      std::vector<double> xPos, std::vector<double> yPos);
};

} /* namespace ipa */

} /* namespace libcamera */
