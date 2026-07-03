/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * Copyright (C) 2026, Ideas On Board
 *
 * AWB control algorithm
 */

#include "awb.h"

#include <algorithm>

#include <libcamera/base/log.h>

#include <libcamera/ipa/core_ipa_interface.h>

#include "libcamera/internal/vector.h"

/**
 * \file awb.h
 */

namespace libcamera {

namespace ipa::rkisp2::algorithms {

LOG_DEFINE_CATEGORY(RkISP2Awb)

class RkISP2AwbStats final : public AwbStats
{
public:
	RkISP2AwbStats() = default;
	RkISP2AwbStats(const RGB<double> means)
		: AwbStats(means)
	{
	}

	/* Minimum mean value below which AWB can't operate. */
	double minColourValue() const override
	{
		return 2.0;
	}
};

namespace {

} /* namespace */

/**
 * \class Awb
 * \brief Manage the white balance with automatic and manual controls
 */

Awb::Awb()
{
}

/**
 * \copydoc libcamera::ipa::Algorithm::init
 */
int Awb::init(IPAContext &context, const ValueNode &tuningData)
{
	return awbAlgo_.init(tuningData, context.ctrlMap);
}

int Awb::configure(IPAContext &context,
		   const IPACameraSensorInfo &configInfo)
{
	awbAlgo_.configure(context.activeState.awb);

	context.configuration.awb.measureWindow.h_offs = 0;
	context.configuration.awb.measureWindow.v_offs = 0;
	/*
	 * Unlike ae lite, this seems to still work when height == full window
	 * height
	 */
	context.configuration.awb.measureWindow.h_size = configInfo.outputSize.width / 15;
	context.configuration.awb.measureWindow.v_size = configInfo.outputSize.height / 15;

	return 0;
}

/**
 * \copydoc libcamera::ipa::Algorithm::queueRequest
 */
void Awb::queueRequest(IPAContext &context, const uint32_t frame,
		       IPAFrameContext &frameContext,
		       const ControlList &controls)
{
	awbAlgo_.queueRequest(context.activeState.awb, frame, frameContext.awb,
			      controls);
}

/**
 * \copydoc libcamera::ipa::Algorithm::prepare
 */
void Awb::prepare([[maybe_unused]] IPAContext &context,
		  [[maybe_unused]] const uint32_t frame,
		  IPAFrameContext &frameContext, RkISP2Params *params)
{
	awbAlgo_.prepare(context.activeState.awb, frameContext.awb);

	auto gainConfig = params->block<RkISP2Blocks::AwbGains>();
	gainConfig.setEnabled(true);

	RGB<double> gains = frameContext.awb.gains;

	/* \todo Use quantized class */
	gainConfig->gains[0].gb = std::clamp<int>(256 * 1.0, 0, 0x3fff);
	gainConfig->gains[0].b = std::clamp<int>(256 * gains.b(), 0, 0x3fff);
	gainConfig->gains[0].r = std::clamp<int>(256 * gains.r(), 0, 0x3fff);
	gainConfig->gains[0].gr = std::clamp<int>(256 * 1.0, 0, 0x3fff);

	auto measConfig = params->block<RkISP2Blocks::AwbMeas>();
	measConfig.setEnabled(true);

	measConfig->meas_window = context.configuration.awb.measureWindow;
	struct rkisp2_isp_awb_color_quad minLimits = { 0, 0, 0, 0 };
	struct rkisp2_isp_awb_color_quad maxLimits = { 255, 255, 255, 255 };
	measConfig->limits[0] = minLimits;
	measConfig->limits[1] = maxLimits;

	for (unsigned int i = 0; i < RKISP2_ISP_AWB_COUNTS_SIZE; i++)
		measConfig->weights[i] = 0x20;
}

/**
 * \copydoc libcamera::ipa::Algorithm::process
 */
void Awb::process([[maybe_unused]] IPAContext &context,
		  [[maybe_unused]] const uint32_t frame,
		  IPAFrameContext &frameContext,
		  const rkisp2_stats_buffer *stats,
		  ControlList &metadata)
{
	RkISP2AwbStats awbStats = calculateRgbMeans(frameContext, stats);

	awbAlgo_.process(context.activeState.awb, frameContext.awb, awbStats,
			 frameContext.lux.lux, metadata);
}

RkISP2AwbStats Awb::calculateRgbMeans([[maybe_unused]] const IPAFrameContext &frameContext,
				      const rkisp2_stats_buffer *stats) const
{
	if (!stats->awb.done) {
		LOG(RkISP2Awb, Error) << "No awb stats";
		return {};
	}

	std::array<int32_t, RKISP2_ISP_AWB_COUNTS_SIZE> counts;
	RGB<double> means;

	for (size_t i = 0; i < RKISP2_ISP_AWB_COUNTS_SIZE; i++)
		counts[i] = static_cast<int32_t>(stats->awb.counts_r[i]);
	means.r() = std::accumulate(counts.begin(), counts.end(), 0) / counts.size();

	for (size_t i = 0; i < RKISP2_ISP_AWB_COUNTS_SIZE; i++)
		counts[i] = static_cast<int32_t>(stats->awb.counts_g[i]);
	means.g() = std::accumulate(counts.begin(), counts.end(), 0) / counts.size();

	for (size_t i = 0; i < RKISP2_ISP_AWB_COUNTS_SIZE; i++)
		counts[i] = static_cast<int32_t>(stats->awb.counts_b[i]);
	means.b() = std::accumulate(counts.begin(), counts.end(), 0) / counts.size();

	for (size_t i = 0; i < RKISP2_ISP_AWB_COUNTS_SIZE; i++)
		counts[i] = static_cast<int32_t>(stats->awb.counts_w[i]);

	return RkISP2AwbStats(means);
}

REGISTER_IPA_ALGORITHM(Awb, "Awb")

} /* namespace ipa::rkisp2::algorithms */

} /* namespace libcamera */
