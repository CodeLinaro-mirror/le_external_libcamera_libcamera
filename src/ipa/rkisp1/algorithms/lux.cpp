/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * Copyright (C) 2024, Ideas On Board
 *
 * RkISP1 Lux control
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

namespace ipa::rkisp1::algorithms {

/**
 * \class Lux
 * \brief RkISP1 Lux control
 *
 * The Lux algorithm is responsible for estimating the lux level of the image.
 * It doesn't take or generate any controls, but it provides a lux level for
 * other algorithms (such as AGC) to use.
 */

/**
 * \copydoc libcamera::ipa::Algorithm::init
 */
int Lux::init([[maybe_unused]] IPAContext &context, const ValueNode &tuningData)
{
	return lux_.init(tuningData);
}

/**
 * \copydoc libcamera::ipa::Algorithm::prepare
 */
void Lux::prepare(IPAContext &context, [[maybe_unused]] const uint32_t frame,
		  IPAFrameContext &frameContext,
		  [[maybe_unused]] RkISP1Params *params)
{
	lux_.prepare(context.activeState.lux, frameContext.lux);
}

/**
 * \copydoc libcamera::ipa::Algorithm::process
 */
void Lux::process(IPAContext &context,
		  [[maybe_unused]] const uint32_t frame,
		  IPAFrameContext &frameContext,
		  const rkisp1_stat_buffer *stats,
		  ControlList &metadata)
{
	if (!stats || !(stats->meas_type & RKISP1_CIF_ISP_STAT_HIST)) {
		lux_.process(context.activeState.lux, frameContext.lux, metadata, {});
		return;
	}

	/* \todo Deduplicate the histogram calculation from AGC */
	const rkisp1_cif_isp_stat *params = &stats->params;
	Histogram yHist({ params->hist.hist_bins, context.hw.numHistogramBins },
			[](uint32_t x) { return x >> 4; });

	lux_.process(
		context.activeState.lux, frameContext.lux, metadata,
		{ {
			.exposureTime = context.configuration.agc.lineDuration *
					frameContext.sensor.exposure,
			.analogueGain = frameContext.sensor.gain,
			.digitalGain = 1,
			.yHist = yHist,
		} });
}

REGISTER_IPA_ALGORITHM(Lux, "Lux")

} /* namespace ipa::rkisp1::algorithms */

} /* namespace libcamera */
