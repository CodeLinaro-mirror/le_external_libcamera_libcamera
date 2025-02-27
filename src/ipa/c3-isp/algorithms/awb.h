/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * Copyright (C) 2024, Amlogic
 *
 * C3ISP AWB control algorithm
 */

#pragma once

#include "algorithm.h"

namespace libcamera {

namespace ipa::c3isp::algorithms {

class Awb : public Algorithm
{
public:
	Awb();
	~Awb() = default;

	int configure(IPAContext &context, const IPACameraSensorInfo &configInfo) override;
	void prepare(IPAContext &context, const uint32_t frame,
		     IPAFrameContext &frameContext,
		     C3ISPParams *params) override;
	void process(IPAContext &context, const uint32_t frame,
		     IPAFrameContext &frameContext,
		     const c3_isp_stats_info *stats,
		     ControlList &metadata) override;

private:
	void fillGainsParam(IPAContext &context, IPAFrameContext &frameContext,
			    C3ISPParams *params);
	void fillConfigParam(IPAContext &context, C3ISPParams *params);
};

} /* namespace ipa::c3isp::algorithms */

} /* namespace libcamera */
