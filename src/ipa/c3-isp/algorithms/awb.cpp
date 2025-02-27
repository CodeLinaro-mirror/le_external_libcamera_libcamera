/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * Copyright (C) 2024, Amlogic
 *
 * C3ISP AWB control algorithm
 */

#include "awb.h"

#include <cmath>

#include <libcamera/base/log.h>
#include <libcamera/base/utils.h>

#include <libcamera/control_ids.h>

#include "libipa/colours.h"
#include "libipa/fixedpoint.h"

/**
 * \file awb.h
 */

namespace libcamera {

namespace ipa::c3isp::algorithms {

/**
 * \class Awb
 * \brief A Grey world white balance correction algorithm
 */

LOG_DEFINE_CATEGORY(C3ISPAwb)

Awb::Awb()
{
}

/**
 * \copydoc libcamera::ipa::Algorithm::configure
 */
int Awb::configure(IPAContext &context,
		   [[maybe_unused]] const IPACameraSensorInfo &configInfo)
{
	context.activeState.awb.rGain = 1.0;
	context.activeState.awb.bGain = 1.0;

	return 0;
}

void Awb::fillGainsParam(IPAContext &context, IPAFrameContext &frameContext,
			 C3ISPParams *params)
{
	double rGain = context.activeState.awb.rGain;
	double bGain = context.activeState.awb.bGain;

	auto AWBGains = params->block<BlockType::AWBGains>();
	AWBGains.setEnabled(C3_ISP_PARAMS_BLOCK_FL_ENABLE);

	AWBGains->gr_gain = floatingToFixedPoint<4, 8, uint16_t, double>(1.0);
	AWBGains->r_gain = floatingToFixedPoint<4, 8, uint16_t, double>(rGain);
	AWBGains->b_gain = floatingToFixedPoint<4, 8, uint16_t, double>(bGain);
	AWBGains->gb_gain = floatingToFixedPoint<4, 8, uint16_t, double>(1.0);

	frameContext.awb.rGain = rGain;
	frameContext.awb.bGain = bGain;
}

void Awb::fillConfigParam(IPAContext &context, C3ISPParams *params)
{
	auto AWBCfg = params->block<BlockType::AWBConfig>();
	AWBCfg.setEnabled(C3_ISP_PARAMS_BLOCK_FL_ENABLE);

	AWBCfg->tap_point = C3_ISP_AWB_STATS_TAP_BEFORE_WB;
	AWBCfg->satur_vald = 1;

	/* We use the full 32x24 zoning scheme */
	AWBCfg->horiz_zones_num = 32;
	AWBCfg->vert_zones_num = 24;

	/* The ratios themselves are stored in Q4.8 format */
	AWBCfg->rg_min = 75;
	AWBCfg->rg_max = 256;
	AWBCfg->bg_min = 44;
	AWBCfg->bg_max = 222;

	/* The triming of ratios are stored in Q4.8 format */
	AWBCfg->rg_low = 93;
	AWBCfg->rg_high = 244;
	AWBCfg->bg_low = 61;
	AWBCfg->bg_high = 205;

	Span<uint8_t> weights{ AWBCfg->zone_weight, C3_ISP_AWB_MAX_ZONES };
	std::fill(weights.begin(), weights.end(), 1);

	Size sensorSize = context.configuration.sensor.size;
	uint8_t maxPointNum =
		std::max(AWBCfg->horiz_zones_num, AWBCfg->vert_zones_num) + 1;

	for (unsigned int i = 0; i < maxPointNum; i++) {
		uint16_t hidx = i * sensorSize.width / AWBCfg->horiz_zones_num;

		/* Aligned with 2 */
		hidx = hidx / 2 * 2;
		AWBCfg->horiz_coord[i] = std::min(hidx, (uint16_t)sensorSize.width);

		uint16_t vidx = i * sensorSize.height / AWBCfg->vert_zones_num;

		/* Aligned with 2 */
		vidx = vidx / 2 * 2;
		AWBCfg->vert_coord[i] = std::min(vidx, (uint16_t)sensorSize.height);
	}
}

/**
 * \copydoc libcamera::ipa::Algorithm::prepare
 */
void Awb::prepare(IPAContext &context, const uint32_t frame,
		  IPAFrameContext &frameContext, C3ISPParams *params)
{
	fillGainsParam(context, frameContext, params);

	if (frame > 0)
		return;

	fillConfigParam(context, params);
}

/**
 * \copydoc libcamera::ipa::Algorithm::process
 */
void Awb::process(IPAContext &context, [[maybe_unused]] const uint32_t frame,
		  IPAFrameContext &frameContext, const c3_isp_stats_info *stats,
		  ControlList &metadata)
{
	IPAActiveState &activeState = context.activeState;
	const struct c3_isp_awb_stats *awb = &stats->awb;

	/* The AWB statistics are R:G and B:G ratios of the zones. */
	uint32_t counted_zones = 0;
	double rgSum = 0, bgSum = 0;

	for (unsigned int i = 0; i < C3_ISP_AWB_MAX_ZONES; i++) {
		if (!awb->stats[i].pixel_sum)
			continue;

		/* The statistics are in Q4.12 format. */
		rgSum += fixedToFloatingPoint<4, 12, double, uint16_t>(awb->stats[i].rg);
		bgSum += fixedToFloatingPoint<4, 12, double, uint16_t>(awb->stats[i].bg);

		counted_zones++;
	}

	double rgAvg, bgAvg;
	if (!counted_zones) {
		rgAvg = 1.0;
		bgAvg = 1.0;
	} else {
		rgAvg = rgSum / counted_zones;
		bgAvg = bgSum / counted_zones;
	}

	/*
	 * To simplify the calculation, the average green is hardcoded
	 * to 1.0.
	 */
	activeState.awb.temperatureK = estimateCCT({ { rgAvg, 1.0, bgAvg } });

	/* Metadata shall contain the up to date measurement */
	metadata.set(controls::ColourTemperature, activeState.awb.temperatureK);

	/*
	 * Estimate the red and blue gains to apply in a grey world.
	 * The green gain is hardcoded to 1.0. Avoid division by zero
	 * by clamping the divisor to mininum value of 0.0625.
	 */
	double rGain = 1 / std::max(rgAvg, 0.0625);
	double bGain = 1 / std::max(bgAvg, 0.0625);

	/* Filter the values to avoid oscillations. */
	double speed = 0.2;
	rGain = speed * rGain + (1 - speed) * activeState.awb.rGain;
	bGain = speed * bGain + (1 - speed) * activeState.awb.bGain;

	activeState.awb.rGain = rGain;
	activeState.awb.bGain = bGain;

	metadata.set(controls::ColourGains, { static_cast<float>(frameContext.awb.rGain),
					      static_cast<float>(frameContext.awb.bGain) });

	LOG(C3ISPAwb, Debug) << "Gains: R " << activeState.awb.rGain
			     << ", B " << activeState.awb.bGain
			     << ", Ct " << activeState.awb.temperatureK << "K";
}

REGISTER_IPA_ALGORITHM(Awb, "Awb")

} /* namespace ipa::c3isp::algorithms */

} /* namespace libcamera */
