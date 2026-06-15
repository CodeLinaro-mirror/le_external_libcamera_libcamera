/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * Copyright (C) 2024-2026 Red Hat Inc.
 *
 * Auto white balance
 */

#include "awb.h"

#include <numeric>
#include <stdint.h>

#include <libcamera/base/log.h>

#include <libcamera/control_ids.h>

#include "libipa/colours.h"

namespace libcamera {

LOG_DEFINE_CATEGORY(IPASoftAwb)

namespace ipa::soft::algorithms {

/*
 * \todo Replace it with a proper Lux algorithm
 */
static constexpr unsigned int kDefaultLux = 500;

class SimpleAwbStats final : public AwbStats
{
public:
	SimpleAwbStats() {}
	SimpleAwbStats(const RGB<double> &rgbMeans)
		: AwbStats(rgbMeans)
	{
	}

	/* Minimum mean value below which AWB can't operate. */
	double minColourValue() const override
	{
		return 0.2;
	}
};

/**
 * \copydoc libcamera::ipa::Algorithm::init
 */
int Awb::init(IPAContext &context, const ValueNode &tuningData)
{
	return awbAlgo_.init(tuningData, context.ctrlMap);
}

/**
 * \copydoc libcamera::ipa::Algorithm::configure
 */
int Awb::configure(IPAContext &context,
		   [[maybe_unused]] const IPAConfigInfo &configInfo)
{
	return awbAlgo_.configure(context.activeState.awb,
				  context.configuration.awb);
}

/**
 * \copydoc libcamera::ipa::Algorithm::queueRequest
 */
void Awb::queueRequest(IPAContext &context,
		       const uint32_t frame,
		       IPAFrameContext &frameContext,
		       const ControlList &controls)
{
	awbAlgo_.queueRequest(context.activeState.awb, frame, frameContext.awb,
			      controls);
}

/**
 * \copydoc libcamera::ipa::Algorithm::prepare
 */
void Awb::prepare(IPAContext &context,
		  [[maybe_unused]] const uint32_t frame,
		  IPAFrameContext &frameContext,
		  DebayerParams *params)
{
	awbAlgo_.prepare(context.activeState.awb, frameContext.awb);

	params->gains = frameContext.awb.gains;
}

SimpleAwbStats Awb::calculateRgbMeans(IPAContext &context,
				      const SwIspStats *stats) const
{
	if (!stats->valid)
		return {};

	const SwIspStats::Histogram &histogram = stats->yHistogram;
	const uint8_t blackLevel = context.activeState.blc.level;

	/*
	 * Black level must be subtracted to get the correct AWB ratios, they
	 * would be off if they were computed from the whole brightness range
	 * rather than from the sensor range.
	 */
	const uint64_t nPixels = std::accumulate(
		histogram.begin(), histogram.end(), uint64_t(0));
	const uint64_t offset = blackLevel * nPixels;
	const uint64_t minValid = 1;

	/*
	 * Make sure the sums are at least minValid, while preventing unsigned
	 * integer underflow.
	 */
	const RGB<uint64_t> sum = stats->sum_.max(offset + minValid) - offset;

	RGB<double> rgbMeans = { { static_cast<double>(sum.r() / nPixels),
				   static_cast<double>(sum.g() / nPixels),
				   static_cast<double>(sum.b() / nPixels) } };

	/*
	 * \todo Determine the minimum allowed thresholds from the mean
	 * but we currently have the sum - not the mean value!
	 *
	 * Currently set to SimpleAwbStats::minColourValue() = 0.2.
	 */
	return SimpleAwbStats(rgbMeans);
}

/**
 * \copydoc libcamera::ipa::Algorithm::process
 */
void Awb::process(IPAContext &context, [[maybe_unused]] const uint32_t frame,
		  IPAFrameContext &frameContext, const SwIspStats *stats,
		  ControlList &metadata)
{
	SimpleAwbStats awbStats = calculateRgbMeans(context, stats);

	awbAlgo_.process(context.activeState.awb, frameContext.awb, awbStats,
			 kDefaultLux, metadata);
}

REGISTER_IPA_ALGORITHM(Awb, "Awb")

} /* namespace ipa::soft::algorithms */

} /* namespace libcamera */
