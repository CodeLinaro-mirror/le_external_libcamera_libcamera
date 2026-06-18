/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * Copyright (C) 2026, Ideas On Board
 *
 * RPP-X1 color correction matrix control algorithm
 */

#pragma once

#include "libcamera/internal/matrix.h"

#include <libipa/ccm.h>

#include "algorithm.h"

namespace libcamera {

namespace ipa::rppx1::algorithms {

class Ccm : public Algorithm
{
public:
	Ccm() {}
	~Ccm() = default;

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
	void parseYaml(const ValueNode &tuningData);
	void setParameters(RppX1Params *params, IPAFrameContext &context);

	unsigned int ct_;
	Interpolator<Matrix<float, 3, 3>> ccm_;
	Interpolator<Matrix<int16_t, 3, 1>> offsets_;

	CcmAlgorithm<Q<4, 12>> ccmAlgo_;
};

} /* namespace ipa::rppx1::algorithms */

} /* namespace libcamera */
