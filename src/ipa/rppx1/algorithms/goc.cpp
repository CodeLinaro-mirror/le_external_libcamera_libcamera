/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * Copyright (C) 2026, Ideas On Board
 *
 * RPP-X1 Gamma out control
 */

#include "goc.h"

/**
 * \file goc.h
 */

namespace libcamera {

namespace ipa::rppx1::algorithms {

/**
 * \copydoc libcamera::ipa::Algorithm::init
 */
int GammaOutCorrection::init(IPAContext &context, const ValueNode &tuningData)
{
	/* The logarithmic segments as specified in the reference. */
	static constexpr unsigned int segments[] = {
		64, 64, 64, 64, 128, 128, 128, 128,
		256, 256, 256, 512, 512, 512, 512, 512,
	};

	return gamma_.init(context.ctrlMap, tuningData, segments);
}

/**
 * \copydoc libcamera::ipa::Algorithm::configure
 */
int GammaOutCorrection::configure(IPAContext &context,
				  [[maybe_unused]] const IPACameraSensorInfo &configInfo)
{
	gamma_.configure(context.activeState.goc);
	return 0;
}

/**
 * \copydoc libcamera::ipa::Algorithm::queueRequest
 */
void GammaOutCorrection::queueRequest(IPAContext &context, const uint32_t frame,
				      IPAFrameContext &frameContext,
				      const ControlList &controls)
{
	gamma_.queueRequest(context.activeState.goc, frame, frameContext.goc, controls);
}

/**
 * \copydoc libcamera::ipa::Algorithm::prepare
 */
void GammaOutCorrection::prepare([[maybe_unused]] IPAContext &context,
				 [[maybe_unused]] const uint32_t frame,
				 IPAFrameContext &frameContext,
				 RppX1Params *params)
{
	if (!frameContext.goc.update)
		return;

	auto config = params->block<BlockType::GaHv>();
	config.setEnabled(true);

	config->mode = RPPX1_GA_SEG_MODE_LOGARITHMIC;
	gamma_.prepare(frameContext.goc, std::span(config->gamma_y));
}

/**
 * \copydoc libcamera::ipa::Algorithm::process
 */
void GammaOutCorrection::process([[maybe_unused]] IPAContext &context,
				 [[maybe_unused]] const uint32_t frame,
				 IPAFrameContext &frameContext,
				 [[maybe_unused]] const RppX1Stats *stats,
				 ControlList &metadata)
{
	metadata.set(controls::Gamma, frameContext.goc.gamma);
}

REGISTER_IPA_ALGORITHM(GammaOutCorrection, "GammaOutCorrection")

} /* namespace ipa::rppx1::algorithms */

} /* namespace libcamera */
