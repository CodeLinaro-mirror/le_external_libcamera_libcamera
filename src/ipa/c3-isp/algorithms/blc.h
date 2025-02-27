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
	Blc();
	~Blc() = default;

	int init(IPAContext &context, const YamlObject &tuningData) override;
	int configure(IPAContext &context,
		      const IPACameraSensorInfo &configInfo) override;
	void prepare(IPAContext &context, const uint32_t frame,
		     IPAFrameContext &frameContext,
		     C3ISPParams *params) override;
	void process(IPAContext &context, const uint32_t frame,
		     IPAFrameContext &frameContext,
		     const c3_isp_stats_info *stats,
		     ControlList &metadata) override;

private:
	uint16_t offsetCompress(uint32_t blackLevel);

	bool tuningBlc_;
	uint32_t offsetR_;
	uint32_t offsetGr_;
	uint32_t offsetGb_;
	uint32_t offsetB_;
};

} /* namespace ipa::c3isp::algorithms */

} /* namespace libcamera */
