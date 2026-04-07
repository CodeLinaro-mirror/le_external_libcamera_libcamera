/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * Copyright (C) 2024, Ideas On Board
 *
 * Simple Lux control
 */

#include "lux.h"

#include <libcamera/base/log.h>

#include <libcamera/control_ids.h>

#include "libipa/histogram.h"
#include "libipa/lux.h"

/**
 * \file lux.h
 */

namespace libcamera {

namespace ipa::soft::algorithms {

/**
 * \class Lux
 * \brief SoftISP Lux control
 *
 * The Lux algorithm is responsible for estimating the lux level of the image.
 * It doesn't take or generate any controls, but it provides a lux level for
 * other algorithms (such as AGC) to use.
 */

/**
 * \brief Construct a SoftISP Lux algo module
 */
Lux::Lux()
{
}

/**
 * \copydoc libcamera::ipa::Algorithm::init
 */
int Lux::init([[maybe_unused]] IPAContext &context, const YamlObject &tuningData)
{
	return lux_.parseTuningData(tuningData);
}

/**
 * \copydoc libcamera::ipa::Algorithm::prepare
 */
void Lux::prepare(IPAContext &context, [[maybe_unused]] const uint32_t frame,
		  IPAFrameContext &frameContext,
		  [[maybe_unused]] DebayerParams *params)
{
	frameContext.lux.lux = context.activeState.lux.lux;
}

/**
 * \copydoc libcamera::ipa::Algorithm::process
 */
void Lux::process(IPAContext &context,
		  [[maybe_unused]] const uint32_t frame,
		  IPAFrameContext &frameContext,
		  const SwIspStats *stats,
		  ControlList &metadata)
{
	/*
	 * Report the lux level used by algorithms to prepare this frame
	 * not the lux level *of* this frame.
	 */
	metadata.set(controls::Lux, frameContext.lux.lux);

	if (!stats)
		return;

	/* Todo: Sensor configuration should move out of AGC */
	utils::Duration exposureTime = context.configuration.agc.lineDuration *
				       frameContext.sensor.exposure;
	double gain = frameContext.sensor.gain;
	double digitalGain = 1.0;

	Histogram yHist(stats->yHistogram);

	context.activeState.lux.lux =
		lux_.estimateLux(exposureTime, gain, digitalGain, yHist);
}

REGISTER_IPA_ALGORITHM(Lux, "Lux")

} /* namespace ipa::soft::algorithms */

} /* namespace libcamera */
