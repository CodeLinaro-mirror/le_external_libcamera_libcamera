/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * Copyright (C) 2026, Robert Bozik
 *
 * Temporal noise reduction parameters
 */

#pragma once

#include "algorithm.h"

namespace libcamera {

namespace ipa::softisp::algorithms {

class Denoise : public Algorithm
{
public:
	Denoise() = default;
	~Denoise() = default;

	int init(IPAContext &context, const ValueNode &tuningData) override;
	void prepare(IPAContext &context, const uint32_t frame,
		     IPAFrameContext &frameContext,
		     DebayerParams *params) override;

private:
	float alpha_;
	float noiseSlope_;
	float noiseFloor_;
	float motionSigma_;
};

} /* namespace ipa::softisp::algorithms */

} /* namespace libcamera */
