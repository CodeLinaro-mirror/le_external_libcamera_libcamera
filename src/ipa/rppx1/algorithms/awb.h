/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * Copyright (C) 2026, Ideas On Board
 *
 * AWB control algorithm
 */

#pragma once

#include "libipa/awb.h"
#include "libipa/fixedpoint.h"

#include "algorithm.h"

namespace libcamera {

namespace ipa::rppx1::algorithms {

class RppX1AwbStats;

class Awb : public Algorithm
{
public:
	Awb() = default;
	~Awb() = default;

	int init(IPAContext &context, const ValueNode &tuningData) override;
	int configure(IPAContext &context, const IPACameraSensorInfo &configInfo) override;
	void queueRequest(IPAContext &context, const uint32_t frame,
			  IPAFrameContext &frameContext,
			  const ControlList &controls) override;
	void prepare(IPAContext &context, const uint32_t frame,
		     IPAFrameContext &frameContext,
		     RppX1Params *params) override;
	void process(IPAContext &context, const uint32_t frame,
		     IPAFrameContext &frameContext,
		     const RppX1Stats *stats,
		     ControlList &metadata) override;

private:
	RppX1AwbStats calculateRgbMeans(const IPAFrameContext &frameContext,
					const RppX1Stats *stats) const;

	AwbAlgorithm<UQ<6, 12>> awbAlgo_;
};

} /* namespace ipa::rppx1::algorithms */
} /* namespace libcamera */
