/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * Copyright (C) 2026, Ideas On Board
 *
 * RPP-X1 Gamma Sensor Linearization control
 */

#pragma once

#include <vector>

#include "algorithm.h"

namespace libcamera {

namespace ipa::rppx1::algorithms {

class GammaSensorLinearization : public Algorithm
{
public:
	int init(IPAContext &context, const ValueNode &tuningData) override;
	void prepare(IPAContext &context, const uint32_t frame,
		     IPAFrameContext &frameContext,
		     RppX1Params *params) override;

private:
	std::vector<uint8_t> gammaDx_;
	std::array<uint32_t, RPPX1_LIN_DEGAMMA_CURVE_NUM> curveYr_;
	std::array<uint32_t, RPPX1_LIN_DEGAMMA_CURVE_NUM> curveYg_;
	std::array<uint32_t, RPPX1_LIN_DEGAMMA_CURVE_NUM> curveYb_;
};

} /* namespace ipa::rppx1::algorithms */

} /* namespace libcamera */
