/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * Copyright (C) 2026, Ideas On Board
 *
 * RPP-X1 Black Level Correction control
 */

#pragma once

#include <stdint.h>

#include "algorithm.h"

namespace libcamera {

namespace ipa::rppx1::algorithms {

class BlackLevelCorrection : public Algorithm
{
public:
	int init(IPAContext &context, const ValueNode &tuningData) override;
	int configure(IPAContext &context,
		      const IPACameraSensorInfo &configInfo) override;
	void prepare(IPAContext &context, const uint32_t frame,
		     IPAFrameContext &frameContext,
		     RppX1Params *params) override;
	void process(IPAContext &context, const uint32_t frame,
		     IPAFrameContext &frameContext,
		     const RppX1Stats *stats,
		     ControlList &metadata) override;

private:
	int16_t blackLevelRed_;
	int16_t blackLevelGreenR_;
	int16_t blackLevelGreenB_;
	int16_t blackLevelBlue_;
};

} /* namespace ipa::rppx1::algorithms */

} /* namespace libcamera */
