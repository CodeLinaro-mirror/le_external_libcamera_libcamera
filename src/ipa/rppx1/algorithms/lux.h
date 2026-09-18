/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * Copyright (C) 2026, Ideas On Board
 *
 * RPP-X1 Lux estimation
 */

#pragma once

#include "libipa/lux.h"

#include "algorithm.h"

namespace libcamera {

namespace ipa::rppx1::algorithms {

class Lux : public Algorithm
{
public:
	int init(IPAContext &context, const ValueNode &tuningData) override;
	void prepare(IPAContext &context, const uint32_t frame,
		     IPAFrameContext &frameContext,
		     RppX1Params *params) override;
	void process(IPAContext &context, const uint32_t frame,
		     IPAFrameContext &frameContext,
		     const RppX1Stats *stats,
		     ControlList &metadata) override;

private:
	ipa::Lux lux_;
};

} /* namespace ipa::rppx1::algorithms */

} /* namespace libcamera */
