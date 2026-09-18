/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * Copyright (C) 2026, Ideas On Board
 *
 * Rpp-X1 AGC/AEC algorithm
 */

#pragma once

#include <map>
#include <vector>

#include "libipa/agc.h"

#include "algorithm.h"

namespace libcamera {

namespace ipa::rppx1::algorithms {

class Agc : public Algorithm
{
public:
	int init(IPAContext &context, const ValueNode &tuningData) override;
	int configure(IPAContext &context, const IPACameraSensorInfo &configInfo) override;
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
	int parseMeteringModes(IPAContext &context, const ValueNode &tuningData);

	std::map<int32_t, std::vector<float>> meteringModes_;
	AgcAlgorithm agc_;
};

} /* namespace ipa::rppx1::algorithms */

} /* namespace libcamera */
