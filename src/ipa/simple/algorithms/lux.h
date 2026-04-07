/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * Copyright (C) 2024, Ideas On Board
 *
 * Simple Lux control
 */

#pragma once

#include <sys/types.h>

#include "libipa/lux.h"

#include "algorithm.h"

namespace libcamera {

namespace ipa::soft::algorithms {

class Lux : public Algorithm
{
public:
	Lux();

	int init(IPAContext &context, const YamlObject &tuningData) override;
	void prepare(IPAContext &context, const uint32_t frame,
		     IPAFrameContext &frameContext,
		     DebayerParams *params) override;
	void process(IPAContext &context, const uint32_t frame,
		     IPAFrameContext &frameContext,
		     const SwIspStats *stats,
		     ControlList &metadata) override;

private:
	ipa::Lux lux_;
};

} /* namespace ipa::soft::algorithms */
} /* namespace libcamera */
