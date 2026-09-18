/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * Copyright (C) 2026, Ideas On Board
 *
 * RPP-X1 Lux estimation
 */

#include "lux.h"

#include <libcamera/base/utils.h>

#include <libcamera/control_ids.h>

#include "libipa/histogram.h"
#include "libipa/lux.h"

/**
 * \file lux.h
 */

namespace libcamera {

namespace ipa::rppx1::algorithms {

/**
 * \copydoc libcamera::ipa::Algorithm::init
 */
int Lux::init([[maybe_unused]] IPAContext &context, const ValueNode &tuningData)
{
	return lux_.parseTuningData(tuningData);
}

/**
 * \copydoc libcamera::ipa::Algorithm::prepare
 */
void Lux::prepare(IPAContext &context, [[maybe_unused]] const uint32_t frame,
		  IPAFrameContext &frameContext,
		  [[maybe_unused]] RppX1Params *params)
{
	frameContext.lux.lux = context.activeState.lux.lux;
}

/**
 * \copydoc libcamera::ipa::Algorithm::process
 */
void Lux::process(IPAContext &context,
		  [[maybe_unused]] const uint32_t frame,
		  IPAFrameContext &frameContext,
		  const RppX1Stats *stats,
		  ControlList &metadata)
{
	/*
	 * Report the lux level used by algorithms to prepare this frame
	 * not the lux level *of* this frame.
	 */
	metadata.set(controls::Lux, frameContext.lux.lux);

	utils::Duration exposureTime = context.configuration.agc.lineDuration *
				       frameContext.sensor.exposure;
	double gain = frameContext.sensor.gain;

	/* \todo Deduplicate the histogram calculation from AGC */
	const auto histPost = stats->block<StatsType::HistPost>();
	if (!histPost)
		return;

	Histogram yHist(histPost->hist_bins, [](uint32_t x) { return x >> 4; });

	context.activeState.lux.lux = lux_.estimateLux(exposureTime, gain, 1.0, yHist);
}

REGISTER_IPA_ALGORITHM(Lux, "Lux")

} /* namespace ipa::rppx1::algorithms */

} /* namespace libcamera */
