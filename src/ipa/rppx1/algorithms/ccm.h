/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * Copyright (C) 2026, Ideas On Board
 *
 * RPP-X1 Color Correction Matrix control algorithm
 */

#pragma once

#include "libipa/ccm.h"
#include "libipa/fixedpoint.h"

#include "algorithm.h"

namespace libcamera {

namespace ipa::rppx1::algorithms {

class Ccm : public Algorithm
{
public:
	int init(IPAContext &context, const ValueNode &tuningData) override;
	int configure(IPAContext &context,
		      const IPACameraSensorInfo &configInfo) override;
	void queueRequest(IPAContext &context,
			  const uint32_t frame,
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
	void setParameters(rppx1_ccor_params &config, IPAFrameContext &context);

	CcmAlgorithm<Q<4, 12>> ccmAlgo_;
};

} /* namespace ipa::rppx1::algorithms */

} /* namespace libcamera */
