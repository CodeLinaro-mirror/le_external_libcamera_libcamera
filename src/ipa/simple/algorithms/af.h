/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * Copyright (C) 2025 Vasiliy Doylov <nekodevelopper@gmail.com>
 *
 * Auto focus
 */

#pragma once

#include "algorithm.h"

namespace libcamera {

namespace ipa::soft::algorithms {

class Af : public Algorithm
{
public:
	Af();
	~Af() = default;

	int init(IPAContext &context, const YamlObject &tuningData) override;
	int configure(IPAContext &context, const IPAConfigInfo &configInfo) override;
	void queueRequest(typename Module::Context &context,
			  const uint32_t frame,
			  typename Module::FrameContext &frameContext,
			  const ControlList &controls)
		override;
	void process(IPAContext &context, const uint32_t frame,
		     IPAFrameContext &frameContext,
		     const SwIspStats *stats,
		     ControlList &metadata) override;

private:
	void updateFocus(IPAContext &context, IPAFrameContext &frameContext, double focus);
};

} /* namespace ipa::soft::algorithms */

} /* namespace libcamera */
