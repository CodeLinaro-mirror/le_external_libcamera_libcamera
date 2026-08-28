/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * Copyright (C) 2026, Ideas On Board
 *
 * RPP-X1 lux estimation algorithm
 */

#include "lux.h"

#include <libcamera/base/utils.h>

#include <libcamera/control_ids.h>

#include "libipa/histogram.h"
#include "libipa/lux.h"

namespace libcamera {

namespace ipa::rppx1::algorithms {

int Lux::init([[maybe_unused]] IPAContext &context, const ValueNode &tuningData)
{
	return lux_.init(tuningData);
}

void Lux::prepare(IPAContext &context, [[maybe_unused]] const uint32_t frame,
		  IPAFrameContext &frameContext,
		  [[maybe_unused]] RppX1Params *params)
{
	lux_.prepare(context.activeState.lux, frameContext.lux);
}

void Lux::process(IPAContext &context,
		  [[maybe_unused]] const uint32_t frame,
		  IPAFrameContext &frameContext,
		  const RppX1Stats *stats,
		  ControlList &metadata)
{
	const auto histPost = stats->block<StatsType::HistPost>();
	if (!histPost) {
		lux_.process(context.activeState.lux, frameContext.lux, metadata, {});
		return;
	}

	lux_.process(
		context.activeState.lux, frameContext.lux, metadata,
		{ {
			.exposureTime = context.configuration.agc.lineDuration *
					frameContext.sensor.exposure,
			.analogueGain = frameContext.sensor.gain,
			.digitalGain = 1,
			/* \todo Deduplicate the histogram calculation from AGC */
			.yHist = {
				histPost->hist_bins,
				[](uint32_t x) { return x >> 4; },
			},
		} });
}

REGISTER_IPA_ALGORITHM(Lux, "Lux")

} /* namespace ipa::rppx1::algorithms */

} /* namespace libcamera */
