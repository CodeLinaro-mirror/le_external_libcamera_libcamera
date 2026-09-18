/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * Copyright (C) 2026, Ideas On Board
 *
 * RPP-X1 Lens Shading Correction control
 */

#pragma once

#include <vector>

#include "libipa/fixedpoint.h"
#include "libipa/lsc.h"

#include "algorithm.h"

namespace libcamera {

namespace ipa::rppx1::algorithms {

class LensShadingCorrection : public Algorithm
{
public:
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
	void setParameters(rppx1_lsc_params &config);
	void copyTable(rppx1_lsc_params &config,
		       const lsc::Components<uint16_t> &set);

	std::vector<double> xSize_;
	std::vector<double> ySize_;
	std::vector<double> xPos_;
	std::vector<double> yPos_;
	uint16_t xGrad_[RPPX1_LSC_NUM_SECTORS];
	uint16_t yGrad_[RPPX1_LSC_NUM_SECTORS];
	uint16_t xSizes_[RPPX1_LSC_NUM_SECTORS];
	uint16_t ySizes_[RPPX1_LSC_NUM_SECTORS];

	unsigned int lastAppliedCt_ = 0;
	unsigned int lastAppliedQuantizedCt_ = 0;

	LscAlgorithm<UQ<2, 10>> lscAlgo_;
};

} /* namespace ipa::rppx1::algorithms */

} /* namespace libcamera */
