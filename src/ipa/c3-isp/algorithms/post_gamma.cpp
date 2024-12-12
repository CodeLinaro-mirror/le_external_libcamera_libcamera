/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * Copyright (C) 2024, Amlogic
 *
 * C3ISP Post Gamma control
 */

#include "post_gamma.h"

#include <libcamera/base/log.h>
#include <libcamera/control_ids.h>

/**
 * \file post_gamma.h
 */

namespace libcamera {

namespace ipa::c3isp::algorithms {

/**
 * \class PostGamma
 * \brief A post gamma algorithm
 */

LOG_DEFINE_CATEGORY(C3ISPPostGamma)

PostGamma::PostGamma()
{
}

/**
 * \copydoc libcamera::ipa::Algorithm::init
 */
int PostGamma::init([[maybe_unused]] IPAContext &context,
		    [[maybe_unused]] const YamlObject &tuningData)
{
	gammaLut =
		tuningData["GammaLut"].getList<uint16_t>().value_or(std::vector<uint16_t>{});

	return 0;
}

/**
 * \copydoc libcamera::ipa::Algorithm::prepare
 */
void PostGamma::prepare([[maybe_unused]] IPAContext &context,
			[[maybe_unused]] const uint32_t frame,
			[[maybe_unused]] IPAFrameContext &frameContext,
			C3ISPParams *params)
{
	auto PostGammaCfg = params->block<BlockType::PostGamma>();
	PostGammaCfg.setEnabled(C3_ISP_PARAMS_BLOCK_FL_NONE);

	for (unsigned int i = 0; i < 129; i++) {
		PostGammaCfg->lut[i] = gammaLut[i];
	}
}

REGISTER_IPA_ALGORITHM(PostGamma, "PostGamma")

} /* namespace ipa::c3isp::algorithms */

} /* namespace libcamera */
