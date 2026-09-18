/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * Copyright (C) 2026, Ideas On Board
 *
 * RPP-X1 Gamma out control
 */

#pragma once

#include <libipa/gamma.h>

#include "algorithm.h"

namespace libcamera {

namespace ipa::rppx1::algorithms {

class GammaOutCorrection : public Algorithm
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
	/* RPP_OUT ("human vision") pipeline with 12-bit gamma values */
	GammaAlgorithm<RPPX1_GA_MAX_SAMPLES, UQ<0, 12>> gamma_;
};

} /* namespace ipa::rppx1::algorithms */

} /* namespace libcamera */
