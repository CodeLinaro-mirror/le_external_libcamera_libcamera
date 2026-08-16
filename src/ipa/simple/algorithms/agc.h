/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * Copyright (C) 2024, Red Hat Inc.
 *
 * Exposure and gain
 */

#pragma once

#include <optional>

#include "algorithm.h"

namespace libcamera {

namespace ipa::soft::algorithms {

class Agc : public Algorithm
{
public:
	Agc();
	~Agc() = default;

	int init(IPAContext &context, const ValueNode &tuningData) override;
	int configure(IPAContext &context,
		      const IPAConfigInfo &configInfo) override;
	void process(IPAContext &context, const uint32_t frame,
		     IPAFrameContext &frameContext,
		     const SwIspStats *stats,
		     ControlList &metadata) override;

private:
	void updateExposure(IPAContext &context, IPAFrameContext &frameContext, double exposureMSV);

	double exposureOptimal_;
	std::optional<double> maxAnalogueGain_;
	std::optional<double> maxExposureTimeMs_;
};

} /* namespace ipa::soft::algorithms */

} /* namespace libcamera */
