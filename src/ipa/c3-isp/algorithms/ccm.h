/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * Copyright (C) 2024, Amlogic
 *
 * C3ISP Color Correction Matrix control
 */

#pragma once

#include <linux/c3-isp-config.h>

#include <libipa/interpolator.h>

#include "algorithm.h"

namespace libcamera {

namespace ipa::c3isp::algorithms {

class Ccm : public Algorithm
{
public:
	Ccm();
	~Ccm() = default;

	int init(IPAContext &context, const YamlObject &tuningData) override;
	void prepare(IPAContext &context, const uint32_t frame,
		     IPAFrameContext &frameContext,
		     C3ISPParams *params) override;
	void process(IPAContext &context, const uint32_t frame,
		     IPAFrameContext &frameContext, const c3_isp_stats_info *stats,
		     ControlList &metadata) override;

private:
	std::vector<int> ccmCoeff;
};

} /* namespace ipa::c3isp::algorithms */

} /* namespace libcamera */
