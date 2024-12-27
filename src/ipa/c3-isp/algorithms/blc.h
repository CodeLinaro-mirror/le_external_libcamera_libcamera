/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * Copyright (C) 2024, Amlogic
 *
 * C3ISP Black Level Correction control
 */

#pragma once

#include "algorithm.h"

namespace libcamera {

namespace ipa::c3isp::algorithms {

class Blc : public Algorithm
{
public:
	Blc() {}
	~Blc() = default;

	int init(IPAContext &context, const YamlObject &tuningData) override;
	void prepare(IPAContext &context, const uint32_t frame,
		     IPAFrameContext &frameContext,
		     C3ISPParams *params) override;
	void process(IPAContext &context, const uint32_t frame,
		     IPAFrameContext &frameContext,
		     const c3_isp_stats_info *stats,
		     ControlList &metadata) override;

private:
	int16_t blackLevelRed_;
	int16_t blackLevelGreenR_;
	int16_t blackLevelGreenB_;
	int16_t blackLevelBlue_;
};

} /* namespace ipa::c3isp::algorithms */

} /* namespace libcamera */
