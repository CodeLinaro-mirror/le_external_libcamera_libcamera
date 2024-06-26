/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * Copyright (C) 2024, Red Hat Inc.
 *
 * black level handling
 */

#pragma once

#include <libcamera/controls.h>

#include "libcamera/internal/software_isp/swisp_stats.h"

#include "algorithm.h"
#include "ipa_context.h"

namespace libcamera {

namespace ipa::soft::algorithms {

class BlackLevel : public Algorithm
{
public:
	BlackLevel();
	~BlackLevel() = default;

	int init(IPAContext &context, const YamlObject &tuningData)
		override;
	void process(IPAContext &context, const uint32_t frame,
		     IPAFrameContext &frameContext,
		     const SwIspStats *stats,
		     ControlList &metadata) override;
};

} /* namespace ipa::soft::algorithms */

} /* namespace libcamera */
