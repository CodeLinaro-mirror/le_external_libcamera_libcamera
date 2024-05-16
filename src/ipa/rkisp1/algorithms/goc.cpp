/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * Copyright (C) 2024 Ideas On Board
 *
 * RkISP1 Gamma out control
 */
#include "goc.h"

#include <cmath>

#include <libcamera/base/log.h>
#include <libcamera/base/utils.h>

#include "libcamera/internal/yaml_parser.h"

#include "linux/rkisp1-config.h"

/**
 * \file gsl.h
 */

namespace libcamera {

namespace ipa::rkisp1::algorithms {

/**
 * \class Gamma
 * \brief RkISP1 Gamma out control
 *
 * This algorithm implements the gamma out curve for the RkISP1.
 * It defaults to a gamma value of 2.2
 * As gamma is internally represented as a piecewise linear function with only
 * 16 knots, the difference between gamma=2.2 and sRGB gamma is minimal.
 * Therefore sRGB gamma was not implemented as special case.
 *
 * Useful links:
 * https://www.cambridgeincolour.com/tutorials/gamma-correction.htm
 * https://en.wikipedia.org/wiki/SRGB
 */

LOG_DEFINE_CATEGORY(RkISP1Gamma)

Gamma::Gamma()
{
}

/**
 * \copydoc libcamera::ipa::Algorithm::init
 */
int Gamma::init([[maybe_unused]] IPAContext &context,
		[[maybe_unused]] const YamlObject &tuningData)
{
	return 0;
}

/**
 * \copydoc libcamera::ipa::Algorithm::prepare
 */
void Gamma::prepare([[maybe_unused]] IPAContext &context,
		    const uint32_t frame,
		    [[maybe_unused]] IPAFrameContext &frameContext,
		    rkisp1_params_cfg *params)
{
	/* The logarithmic segments as specified in the reference.
	   Plus an additional 0 to make the loop easier */
	int segments[] = { 64, 64, 64, 64, 128, 128, 128, 128, 256, 256, 256,
			   512, 512, 512, 512, 512, 0 };
	auto gamma_y = params->others.goc_config.gamma_y;

	if (frame > 0)
		return;

	int x = 0;
	for (int i = 0; i < RKISP1_CIF_ISP_GAMMA_OUT_MAX_SAMPLES_V10; i++) {
		gamma_y[i] = std::pow(x / 4096.0, 1.0 / gamma_) * 1023.0;
		x += segments[i];
	}

	params->others.goc_config.mode = RKISP1_CIF_ISP_GOC_MODE_LOGARITHMIC;
	params->module_en_update |= RKISP1_CIF_ISP_MODULE_GOC;
	params->module_ens |= RKISP1_CIF_ISP_MODULE_GOC;
	params->module_cfg_update |= RKISP1_CIF_ISP_MODULE_GOC;
}

REGISTER_IPA_ALGORITHM(Gamma, "Gamma")

} /* namespace ipa::rkisp1::algorithms */

} /* namespace libcamera */
