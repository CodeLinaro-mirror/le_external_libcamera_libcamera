/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * Copyright (C) 2024, Amlogic
 *
 * C3ISP Color Space Conversion control
 */

#pragma once

#include "algorithm.h"

namespace libcamera {

namespace ipa::c3isp::algorithms {

class Csc : public Algorithm
{
public:
	Csc();
	~Csc() = default;

	int init(IPAContext &context, const YamlObject &tuningData) override;
	void prepare(IPAContext &context, const uint32_t frame,
		     IPAFrameContext &frameContext,
		     C3ISPParams *params) override;
private:
	std::vector<int16_t> cscCoeff_;
};

} /* namespace ipa::c3isp::algorithms */

} /* namespace libcamera */
