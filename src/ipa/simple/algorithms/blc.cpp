/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * Copyright (C) 2024, Red Hat Inc.
 *
 * black level handling
 */

#include "blc.h"

#include <numeric>

#include <libcamera/base/log.h>

namespace libcamera {

namespace ipa::soft::algorithms {

LOG_DEFINE_CATEGORY(IPASoftBL)

BlackLevel::BlackLevel()
{
}

int BlackLevel::init(IPAContext &context,
		     [[maybe_unused]] const YamlObject &tuningData)
{
	context.configuration.black.set = false;
	context.configuration.black.changed = true;
	context.configuration.black.level = 255;
	return 0;
}

void BlackLevel::process(IPAContext &context,
			 [[maybe_unused]] const uint32_t frame,
			 [[maybe_unused]] IPAFrameContext &frameContext,
			 const SwIspStats *stats,
			 [[maybe_unused]] ControlList &metadata)
{
	if (context.configuration.black.set) {
		context.configuration.black.changed = false;
		return;
	}

	const SwIspStats::Histogram &histogram = stats->yHistogram;

	/*
	 * The constant is selected to be "good enough", not overly conservative or
	 * aggressive. There is no magic about the given value.
	 */
	constexpr float ignoredPercentage_ = 0.02;
	const unsigned int total =
		std::accumulate(begin(histogram), end(histogram), 0);
	const unsigned int pixelThreshold = ignoredPercentage_ * total;
	const unsigned int histogramRatio = 256 / SwIspStats::kYHistogramSize;
	const unsigned int currentBlackIdx =
		context.configuration.black.level / histogramRatio;

	for (unsigned int i = 0, seen = 0;
	     i < currentBlackIdx && i < SwIspStats::kYHistogramSize;
	     i++) {
		seen += histogram[i];
		if (seen >= pixelThreshold) {
			context.configuration.black.level = i * histogramRatio;
			context.configuration.black.changed = true;
			LOG(IPASoftBL, Debug)
				<< "Auto-set black level: "
				<< i << "/" << SwIspStats::kYHistogramSize
				<< " (" << 100 * (seen - histogram[i]) / total << "% below, "
				<< 100 * seen / total << "% at or below)";
			break;
		}
	};
}

REGISTER_IPA_ALGORITHM(BlackLevel, "BlackLevel")

} /* namespace ipa::soft::algorithms */

} /* namespace libcamera */
