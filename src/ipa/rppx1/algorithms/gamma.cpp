/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * Copyright (C) 2026, Ideas On Board
 *
 * RPP-X1 gamma correction algorithm
 */

#include "gamma.h"

namespace libcamera {

namespace ipa::rppx1::algorithms {

int Gamma::init(IPAContext &context, const ValueNode &tuningData)
{
	/*
	 * The segments as specified in the reference for
	 * RPPX1_GA_SEG_MODE_LOGARITHMIC.
	 */
	static constexpr unsigned int segments[] = {
		64, 64, 64, 64, 128, 128, 128, 128,
		256, 256, 256, 512, 512, 512, 512, 512,
	};

	return gamma_.init(context.ctrlMap, tuningData, segments);
}

int Gamma::configure(IPAContext &context,
		     [[maybe_unused]] const IPACameraSensorInfo &configInfo)
{
	gamma_.configure(context.activeState.gamma);
	return 0;
}

void Gamma::queueRequest(IPAContext &context, const uint32_t frame,
			 IPAFrameContext &frameContext,
			 const ControlList &controls)
{
	gamma_.queueRequest(context.activeState.gamma, frame,
			    frameContext.gamma, controls);
}

void Gamma::prepare([[maybe_unused]] IPAContext &context,
		    [[maybe_unused]] const uint32_t frame,
		    IPAFrameContext &frameContext,
		    RppX1Params *params)
{
	if (!frameContext.gamma.update)
		return;

	auto config = params->block<BlockType::GaHv>();
	config.setEnabled(true);

	config->mode = RPPX1_GA_SEG_MODE_LOGARITHMIC;
	gamma_.prepare(frameContext.gamma, std::span(config->gamma_y));
}

void Gamma::process([[maybe_unused]] IPAContext &context,
		    [[maybe_unused]] const uint32_t frame,
		    IPAFrameContext &frameContext,
		    [[maybe_unused]] const RppX1Stats *stats,
		    ControlList &metadata)
{
	metadata.set(controls::Gamma, frameContext.gamma.gamma);
}

REGISTER_IPA_ALGORITHM(Gamma, "Gamma")

} /* namespace ipa::rppx1::algorithms */

} /* namespace libcamera */
