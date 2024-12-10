/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * Copyright (C) 2024, Red Hat Inc.
 *
 * Color correction matrix
 */

#pragma once

#include "libcamera/internal/matrix.h"

#include <libipa/interpolator.h>

#include "algorithm.h"

namespace libcamera {

namespace ipa::soft::algorithms {

class Ccm : public Algorithm
{
public:
	Ccm() = default;
	~Ccm() = default;

	int init(IPAContext &context, const YamlObject &tuningData) override;
	int configure(IPAContext &context, const IPAConfigInfo &configInfo) override;
	void prepare(IPAContext &context,
		     const uint32_t frame,
		     IPAFrameContext &frameContext,
		     DebayerParams *params) override;
	void process(IPAContext &context, const uint32_t frame,
		     IPAFrameContext &frameContext,
		     const SwIspStats *stats,
		     ControlList &metadata) override;

private:
	static unsigned int kTemperatureThreshold;
	unsigned int ct_;
	bool ccmEnabled_;
	Interpolator<Matrix<double, 3, 3>> ccm_;
};

} /* namespace ipa::soft::algorithms */

} /* namespace libcamera */
