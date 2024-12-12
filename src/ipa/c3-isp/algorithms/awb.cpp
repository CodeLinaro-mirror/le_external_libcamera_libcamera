/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * Copyright (C) 2024, Amlogic
 *
 * C3ISP AWB control algorithm
 */

#include "awb.h"

#include <algorithm>
#include <ios>

#include <libcamera/base/log.h>

#include <libcamera/control_ids.h>

#include <libcamera/ipa/core_ipa_interface.h>

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
	: horizonalZonesNum_(32), verticalZonesNum_(24)
{
}

/**
 * \copydoc libcamera::ipa::Algorithm::configure
 */
int Awb::configure(IPAContext &context,
		   [[maybe_unused]] const IPACameraSensorInfo &configInfo)
{
	IPAActiveState &activeState = context.activeState;

	activeState.awb.gains.manual.red = 1.0;
	activeState.awb.gains.manual.blue = 1.0;
	activeState.awb.gains.manual.green = 1.0;

	activeState.awb.gains.automatic.red = 1.0;
	activeState.awb.gains.automatic.blue = 1.0;
	activeState.awb.gains.automatic.green = 1.0;
	activeState.awb.autoEnabled = true;

	return 0;
}

/**
 * \copydoc libcamera::ipa::Algorithm::queueRequest
 */
void Awb::queueRequest(IPAContext &context,
		       [[maybe_unused]] const uint32_t frame,
		       IPAFrameContext &frameContext,
		       const ControlList &controls)
{
	auto &awb = context.activeState.awb;

	const auto &awbEnable = controls.get(controls::AwbEnable);
	if (awbEnable && *awbEnable != awb.autoEnabled) {
		awb.autoEnabled = *awbEnable;

		LOG(C3ISPAwb, Debug)
			<< (*awbEnable ? "Enabling" : "Disabling") << " AWB";
	}

	const auto &colourGains = controls.get(controls::ColourGains);
	if (colourGains && !awb.autoEnabled) {
		awb.gains.manual.red = (*colourGains)[0];
		awb.gains.manual.blue = (*colourGains)[1];

		LOG(C3ISPAwb, Debug)
			<< "Set colour gains to red: " << awb.gains.manual.red
			<< ", blue: " << awb.gains.manual.blue;
	}

	frameContext.awb.autoEnabled = awb.autoEnabled;

	if (!awb.autoEnabled) {
		frameContext.awb.gains.red = awb.gains.manual.red;
		frameContext.awb.gains.green = 1.0;
		frameContext.awb.gains.blue = awb.gains.manual.blue;
	}
}

/**
 * \copydoc libcamera::ipa::Algorithm::prepare
 */
void Awb::prepare(IPAContext &context, const uint32_t frame,
		  IPAFrameContext &frameContext, C3ISPParams *params)
{
	/*
	 * This is the latest time we can read the active state. This is the
	 * most up-to-date automatic values we can read.
	 */
	if (frameContext.awb.autoEnabled) {
		frameContext.awb.gains.red = context.activeState.awb.gains.automatic.red;
		frameContext.awb.gains.green = context.activeState.awb.gains.automatic.green;
		frameContext.awb.gains.blue = context.activeState.awb.gains.automatic.blue;
	}

	auto AWBGains = params->block<BlockType::AWBGains>();
	AWBGains.setEnabled(C3_ISP_PARAMS_BLOCK_FL_NONE);

	AWBGains->gr_gain = std::clamp<int>(256 * frameContext.awb.gains.green, 0, 0xfff);
	AWBGains->r_gain = std::clamp<int>(256 * frameContext.awb.gains.red, 0, 0xfff);
	AWBGains->b_gain = std::clamp<int>(256 * frameContext.awb.gains.blue, 0, 0xfff);
	AWBGains->gb_gain = std::clamp<int>(256 * frameContext.awb.gains.green, 0, 0xfff);

	if (frame)
		return;

	auto AWBCfg = params->block<BlockType::AWBConfig>();
	AWBCfg.setEnabled(C3_ISP_PARAMS_BLOCK_FL_NONE);

	AWBCfg->tap_point = C3_ISP_AWB_STATS_TAP_BEFORE_WB;
	AWBCfg->satur_vald = 1;
	AWBCfg->horiz_zones_num = horizonalZonesNum_;
	AWBCfg->vert_zones_num = verticalZonesNum_;
	AWBCfg->rg_min = 75;
	AWBCfg->rg_max = 256;
	AWBCfg->bg_min = 44;
	AWBCfg->bg_max = 222;
	AWBCfg->rg_low = 93;
	AWBCfg->rg_high = 244;
	AWBCfg->bg_low = 61;
	AWBCfg->bg_high = 205;

	for (unsigned int i = 0; i < AWBCfg->horiz_zones_num * AWBCfg->vert_zones_num; i++)
		AWBCfg->zone_weight[i] = 1;

	Size sensorSize = context.configuration.sensor.size;
	uint8_t maxPointNum = std::max(AWBCfg->horiz_zones_num, AWBCfg->vert_zones_num) + 1;

	for (unsigned int i = 0; i < maxPointNum; i++) {
		uint16_t hidx = i * sensorSize.width / AWBCfg->horiz_zones_num;

		/* Aligned with 2 */
		hidx = hidx / 2 * 2;
		AWBCfg->horiz_cood[i] = std::min(hidx, (uint16_t)sensorSize.width);

		uint16_t vidx = i * sensorSize.height / AWBCfg->vert_zones_num;

		/* Aligned with 2 */
		vidx = vidx / 2 * 2;
		AWBCfg->vert_cood[i] = std::min(vidx, (uint16_t)sensorSize.height);
	}
}

uint32_t Awb::estimateCCT(double red, double green, double blue)
{
	/* Convert the RGB values to CIE tristimulus values (XYZ) */
	double X = (-0.14282) * (red) + (1.54924) * (green) + (-0.95641) * (blue);
	double Y = (-0.32466) * (red) + (1.57837) * (green) + (-0.73191) * (blue);
	double Z = (-0.68202) * (red) + (0.77073) * (green) + (0.56332) * (blue);

	/* Calculate the normalized chromaticity values */
	double x = X / (X + Y + Z);
	double y = Y / (X + Y + Z);

	/* Calculate CCT */
	double n = (x - 0.3320) / (0.1858 - y);
	return 449 * n * n * n + 3525 * n * n + 6823.3 * n + 5520.33;
}

/**
 * \copydoc libcamera::ipa::Algorithm::process
 */
void Awb::process([[maybe_unused]] IPAContext &context,
		  [[maybe_unused]] const uint32_t frame,
		  IPAFrameContext &frameContext,
		  [[maybe_unused]] const c3_isp_stats_buffer *stats,
		  ControlList &metadata)
{
	IPAActiveState &activeState = context.activeState;
	const struct c3_isp_awb_stats *awb = &stats->awb;
	uint16_t zoneCnt = horizonalZonesNum_ * verticalZonesNum_;
	uint32_t rgSum = 0;
	uint32_t bgSum = 0;
	double rgMean;
	double bgMean;
	double greenMean;
	double blueMean;
	double redMean;

	for (unsigned int i = 0; i < zoneCnt; i++) {
		rgSum += awb->stats[i].rg;
		bgSum += awb->stats[i].bg;
	}

	rgMean = rgSum / zoneCnt / 4096.0;
	bgMean = bgSum / zoneCnt / 4096.0;

	/*
	 * To simplify the calculation,
	 * the green mean is hardcoded to 1.0
	 */

	greenMean = 1.0;
	redMean = rgMean * greenMean;
	blueMean = bgMean * greenMean;

	activeState.awb.temperatureK = estimateCCT(redMean, greenMean, blueMean);

	/* Metadata shall contain the up to date measurement */
	metadata.set(controls::ColourTemperature, activeState.awb.temperatureK);

	/*
	 * Estimate the red and blue gains to apply in a grey world.
	 * The green gain is hardcoded to 1.0. Avoid division by zero
	 * by clamping the divisor to mininum value of 0.0625.
	 */
	double redGain = greenMean / std::max(redMean, 0.0625);
	double blueGain = greenMean / std::max(blueMean, 0.0625);

	/*
	 * Clamp the gain values to the hardware, which express gains as Q4.8
	 * unsigned integer values. Set the minimum just above zero to avoid
	 * divisions by zero.
	 */
	redGain = std::clamp(redGain, 1.0 / 256, 4095.0 / 256);
	blueGain = std::clamp(blueGain, 1.0 / 256, 4095.0 / 256);

	/* Filter the values to avoid oscillations. */
	double speed = 0.2;
	redGain = speed * redGain + (1 - speed) * activeState.awb.gains.automatic.red;
	blueGain = speed * blueGain + (1 - speed) * activeState.awb.gains.automatic.blue;

	activeState.awb.gains.automatic.red = redGain;
	activeState.awb.gains.automatic.blue = blueGain;
	activeState.awb.gains.automatic.green = 1.0;

	metadata.set(controls::AwbEnable, frameContext.awb.autoEnabled);
	metadata.set(controls::ColourGains, { static_cast<float>(frameContext.awb.gains.red),
					      static_cast<float>(frameContext.awb.gains.blue) });

	LOG(C3ISPAwb, Debug) << "Gains: R " << activeState.awb.gains.automatic.red
			     << ", G " << activeState.awb.gains.automatic.green
			     << ", B " << activeState.awb.gains.automatic.blue
			     << ", Ct " << activeState.awb.temperatureK << "K";
}

REGISTER_IPA_ALGORITHM(Awb, "Awb")

} /* namespace ipa::c3isp::algorithms */

} /* namespace libcamera */
