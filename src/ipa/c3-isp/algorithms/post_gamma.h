/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * Copyright (C) 2024, Amlogic
 *
 * C3ISP Post Gamma control
 */

#pragma once

#include "algorithm.h"

namespace libcamera {

namespace ipa::c3isp::algorithms {

class PostGamma : public Algorithm
{
public:
	PostGamma();
	~PostGamma() = default;

	int init(IPAContext &context, const YamlObject &tuningData) override;
	void prepare(IPAContext &context, const uint32_t frame,
		     IPAFrameContext &frameContext,
		     C3ISPParams *params) override;

private:
	std::vector<uint16_t> gammaLut_;
};

} /* namespace ipa::c3isp::algorithms */

} /* namespace libcamera */
