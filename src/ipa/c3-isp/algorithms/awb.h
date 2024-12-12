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
	void queueRequest(IPAContext &context, const uint32_t frame,
			  IPAFrameContext &frameContext,
			  const ControlList &controls) override;
	void prepare(IPAContext &context, const uint32_t frame,
		     IPAFrameContext &frameContext,
		     C3ISPParams *params) override;
	void process(IPAContext &context, const uint32_t frame,
		     IPAFrameContext &frameContext,
		     const c3_isp_stats_buffer *stats,
		     ControlList &metadata) override;

private:
	uint32_t estimateCCT(double red, double green, double blue);
	uint8_t horizonalZonesNum_;
	uint8_t verticalZonesNum_;
};

} /* namespace ipa::c3isp::algorithms */

} /* namespace libcamera */
