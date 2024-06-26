/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * Copyright (C) 2024, Red Hat Inc.
 *
 * Color gains (AWB + gamma)
 */

#include "colors.h"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <stdint.h>

#include <libcamera/base/log.h>

#include "simple/ipa_context.h"

namespace libcamera {

LOG_DEFINE_CATEGORY(IPASoftColors)

namespace ipa::soft::algorithms {

static constexpr unsigned int kGammaLookupSize = 1024;

Colors::Colors()
{
}

int Colors::init(IPAContext &context,
		 [[maybe_unused]] const YamlObject &tuningData)
{
	/* Gamma value is fixed */
	context.configuration.gamma = 0.5;
	updateGammaTable(context);

	auto &gains = context.activeState.gains;
	gains.red = gains.green = gains.blue = 256;

	return 0;
}

void Colors::updateGammaTable(IPAContext &context)
{
	auto &gammaTable = context.activeState.gammaTable;
	const unsigned int blackIndex =
		context.configuration.black.level * IPAActiveState::kGammaLookupSize / 256;
	std::fill(gammaTable.begin(), gammaTable.begin() + blackIndex, 0);
	const float divisor = kGammaLookupSize - blackIndex - 1.0;
	for (unsigned int i = blackIndex; i < kGammaLookupSize; i++)
		gammaTable[i] = UINT8_MAX * powf((i - blackIndex) / divisor,
						 context.configuration.gamma);
}

void Colors::prepare(IPAContext &context,
		     [[maybe_unused]] const uint32_t frame,
		     [[maybe_unused]] IPAFrameContext &frameContext,
		     DebayerParams *params)
{
	/* Update the gamma table if needed */
	if (context.configuration.black.changed)
		updateGammaTable(context);

	auto &gains = context.activeState.gains;
	auto &gammaTable = context.activeState.gammaTable;
	for (unsigned int i = 0; i < DebayerParams::kRGBLookupSize; i++) {
		constexpr unsigned int div =
			static_cast<double>(DebayerParams::kRGBLookupSize) * 256 / kGammaLookupSize;
		/* Apply gamma after gain! */
		unsigned int idx;
		idx = std::min({ i * gains.red / div, kGammaLookupSize - 1 });
		params->red[i] = gammaTable[idx];
		idx = std::min({ i * gains.green / div, kGammaLookupSize - 1 });
		params->green[i] = gammaTable[idx];
		idx = std::min({ i * gains.blue / div, kGammaLookupSize - 1 });
		params->blue[i] = gammaTable[idx];
	}
}

void Colors::process(IPAContext &context,
		     [[maybe_unused]] const uint32_t frame,
		     [[maybe_unused]] IPAFrameContext &frameContext,
		     const SwIspStats *stats,
		     [[maybe_unused]] ControlList &metadata)
{
	const SwIspStats::Histogram &histogram = stats->yHistogram;
	const uint8_t blackLevel = context.configuration.black.level;

	/*
	 * Black level must be subtracted to get the correct AWB ratios, they
	 * would be off if they were computed from the whole brightness range
	 * rather than from the sensor range.
	 */
	const uint64_t nPixels = std::accumulate(
		histogram.begin(), histogram.end(), 0);
	const uint64_t offset = blackLevel * nPixels;
	const uint64_t sumR = stats->sumR_ - offset / 4;
	const uint64_t sumG = stats->sumG_ - offset / 2;
	const uint64_t sumB = stats->sumB_ - offset / 4;

	/*
	 * Calculate red and blue gains for AWB.
	 * Clamp max gain at 4.0, this also avoids 0 division.
	 * Gain: 128 = 0.5, 256 = 1.0, 512 = 2.0, etc.
	 */
	auto &gains = context.activeState.gains;
	gains.red = sumR <= sumG / 4 ? 1024 : 256 * sumG / sumR;
	gains.blue = sumB <= sumG / 4 ? 1024 : 256 * sumG / sumB;
	/* Green gain is fixed to 256 */

	LOG(IPASoftColors, Debug) << "gain R/B " << gains.red << "/" << gains.blue;
}

REGISTER_IPA_ALGORITHM(Colors, "Colors")

} /* namespace ipa::soft::algorithms */

} /* namespace libcamera */
