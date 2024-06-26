/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * Copyright (C) 2024, Red Hat Inc.
 *
 * Color gains (AWB + gamma)
 */

#pragma once

#include <libcamera/controls.h>

#include "libcamera/internal/software_isp/debayer_params.h"
#include "libcamera/internal/software_isp/swisp_stats.h"

#include "algorithm.h"
#include "ipa_context.h"

namespace libcamera {

namespace ipa::soft::algorithms {

class Colors : public Algorithm
{
public:
	Colors();
	~Colors() = default;

	int init(IPAContext &context, const YamlObject &tuningData)
		override;
	void prepare(IPAContext &context,
		     const uint32_t frame,
		     IPAFrameContext &frameContext,
		     DebayerParams *params) override;
	void process(IPAContext &context,
		     const uint32_t frame,
		     IPAFrameContext &frameContext,
		     const SwIspStats *stats,
		     ControlList &metadata) override;

private:
	void updateGammaTable(IPAContext &context);
};

} /* namespace ipa::soft::algorithms */

} /* namespace libcamera */
