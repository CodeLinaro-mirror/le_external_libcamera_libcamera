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

#include <libcamera/control_ids.h>

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
 * \brief Configure the Gamma given a configInfo
 * \param[in] context The shared IPA context
 * \param[in] configInfo The IPA configuration data
 *
 * \return 0
 */
int Gamma::configure(IPAContext &context,
		     [[maybe_unused]] const IPACameraSensorInfo &configInfo)
{
	context.activeState.gamma = 2.2;
	return 0;
}

/**
 * \copydoc libcamera::ipa::Algorithm::queueRequest
 */
void Gamma::queueRequest([[maybe_unused]] IPAContext &context,
			 [[maybe_unused]] const uint32_t frame,
			 IPAFrameContext &frameContext,
			 const ControlList &controls)
{
	const auto &gamma = controls.get(controls::Gamma);
	if (gamma) {
		/* \todo This is not correct as it updates the current state with a
		 * future value. But it does no harm at the moment an allows us to
		 * track the last active gamma
		 */
		context.activeState.gamma = *gamma;
		LOG(RkISP1Gamma, Debug) << "Set gamma to " << *gamma;
	}

	frameContext.gamma = context.activeState.gamma;
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

	if (frame == 0 || std::fabs(frameContext.gamma - gamma_) > 0.001) {
		gamma_ = frameContext.gamma;

		int x = 0;
		for (int i = 0; i < RKISP1_CIF_ISP_GAMMA_OUT_MAX_SAMPLES_V10; i++) {
			gamma_y[i] = std::pow(x / 4096.0, 1.0 / gamma_) * 1023.0;
			x += segments[i];
		}

		params->others.goc_config.mode = RKISP1_CIF_ISP_GOC_MODE_LOGARITHMIC;
		params->module_cfg_update |= RKISP1_CIF_ISP_MODULE_GOC;

		/* It is unclear why these bits need to be set more than once.
		 * Setting them only on frame 0 didn't apply gamma.
		 */
		params->module_en_update |= RKISP1_CIF_ISP_MODULE_GOC;
		params->module_ens |= RKISP1_CIF_ISP_MODULE_GOC;
	}
}

/**
 * \copydoc libcamera::ipa::Algorithm::process
 */
void Gamma::process([[maybe_unused]] IPAContext &context,
		    [[maybe_unused]] const uint32_t frame,
		    IPAFrameContext &frameContext,
		    [[maybe_unused]] const rkisp1_stat_buffer *stats,
		    ControlList &metadata)
{
	metadata.set(controls::Gamma, frameContext.gamma);
}

REGISTER_IPA_ALGORITHM(Gamma, "Gamma")

} /* namespace ipa::rkisp1::algorithms */

} /* namespace libcamera */
