/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * Copyright (C) 2024, Red Hat Inc.
 *
 * Exposure and gain
 */

#include "agc_simple.h"

#include <algorithm>
#include <cmath>

#include <linux/v4l2-controls.h>

#include <libcamera/base/log.h>

namespace libcamera {

namespace ipa::soft {

LOG_DEFINE_CATEGORY(IPASoftAgcSimple)


/*
 * The number of bins to use for the optimal exposure calculations.
 */
static constexpr unsigned int kExposureBinsCount = 5;

/*
 * The exposure is optimal when the mean sample value of the histogram is
 * in the middle of the range.
 */
static constexpr float kExposureOptimal = kExposureBinsCount / 2.0;

/*
 * This implements the hysteresis for the exposure adjustment.
 * It is small enough to have the exposure close to the optimal, and is big
 * enough to prevent the exposure from wobbling around the optimal value.
 */
static constexpr float kExposureSatisfactory = 0.2;

/*
 * Proportional gain for exposure/gain adjustment. Maps the MSV error to a
 * multiplicative correction factor:
 *
 *   factor = 1.0 + kExpProportionalGain * error
 *
 * With kExpProportionalGain = 0.04:
 *   - max error ~2.5 -> factor 1.10 (~10% step, same as before)
 *   - error 1.0      -> factor 1.04 (~4% step)
 *   - error 0.3      -> factor 1.012 (~1.2% step)
 *
 * This replaces the fixed 10% bang-bang step with a proportional correction
 * that converges smoothly and avoids overshooting near the target.
 */
static constexpr float kExpProportionalGain = 0.04;

/*
 * Maximum multiplicative step per frame, to bound the correction when the
 * scene changes dramatically.
 */
static constexpr float kExpMaxStep = 0.15;

void AgcSimpleAlgorithm::updateExposure(const Session &session, ActiveState &state, FrameContext &frameContext,
					const ProcessParams &params, double exposureMSV)
{
	double error = kExposureOptimal - exposureMSV;
	if (std::abs(error) <= kExposureSatisfactory)
		return;

	utils::Duration exposureDuration = params.exposure * session.lineDuration;
	int32_t exposure = params.exposure;
	double again = params.gain;

	/*
	 * Compute a proportional correction factor. The sign of the error
	 * determines the direction: positive error means too dark (increase),
	 * negative means too bright (decrease).
	 */
	float step = std::clamp(static_cast<float>(error) * kExpProportionalGain,
				-kExpMaxStep, kExpMaxStep);
	float factor = 1.0f + step;

	const auto limits = AgcAlgorithm::calculateLimits(session, frameContext);

	if (factor > 1.0f) {
		/* Scene too dark: increase exposure first, then gain. */
		if (exposureDuration < limits.exposure.second) {
			int32_t next = static_cast<int32_t>(exposure * factor);
			exposure = std::max(next, exposure + 1);
		} else {
			double next = again * factor;
			if (next - again < session.gainMinStep)
				again += session.gainMinStep;
			else
				again = next;
		}
	} else {
		/* Scene too bright: decrease gain first, then exposure. */
		if (again > std::max(session.gain10, limits.gain.first)) {
			double next = again * factor;
			if (again - next < session.gainMinStep)
				again -= session.gainMinStep;
			else
				again = next;
		} else {
			int32_t next = static_cast<int32_t>(exposure * factor);
			exposure = std::min(next, exposure - 1);
		}
	}

	exposureDuration = std::clamp<utils::Duration>(
		exposure * session.lineDuration,
		limits.exposure.first, limits.exposure.second);
	exposure = exposureDuration / session.lineDuration;
	again = std::clamp(again, limits.gain.first, limits.gain.second);

	state.automatic.exposure = exposure;
	state.automatic.gain = again;

	LOG(IPASoftAgcSimple, Debug)
		<< "exposureMSV " << exposureMSV
		<< " error " << error << " factor " << factor
		<< " exp " << exposure << " again " << again;
}

int AgcSimpleAlgorithm::configure(Session &session, ActiveState &state, const ConfigurationParams &config)
{
	int ret = AgcAlgorithm::configure(session, state, config);
	if (ret)
		return ret;

	const ControlInfo &v4l2Gain = config.sensorControls.find(V4L2_CID_ANALOGUE_GAIN)->second;
	auto defGain = v4l2Gain.def().get<int32_t>();

	if (config.sensor) {
		session.gain10 = std::max(session.minAnalogueGain, 1.0);
		session.gainMinStep = (session.maxAnalogueGain - session.minAnalogueGain) / 100.0;
	} else {
		session.gain10 = defGain;
		session.gainMinStep = 1.0;
	}

	return 0;
}

void AgcSimpleAlgorithm::process(const Session &session, ActiveState &state,
				 FrameContext &frameContext, std::optional<ProcessParams> &&params,
				 ControlList &metadata)
{
	utils::Duration newExposureTime = {};

	if (params) {
		/*
		* Calculate Mean Sample Value (MSV) according to formula from:
		* https://www.araa.asn.au/acra/acra2007/papers/paper84final.pdf
		*/
		const auto &histogram = params->stats.yHistogram;
		const unsigned int blackLevelHistIdx = params->blackLevel / (256 / SwIspStats::kYHistogramSize);
		const unsigned int histogramSize =
			SwIspStats::kYHistogramSize - blackLevelHistIdx;
		const unsigned int yHistValsPerBin = histogramSize / kExposureBinsCount;
		const unsigned int yHistValsPerBinMod =
			histogramSize / (histogramSize % kExposureBinsCount + 1);
		int exposureBins[kExposureBinsCount] = {};
		unsigned int denom = 0;
		unsigned int num = 0;

		if (yHistValsPerBin == 0) {
			LOG(IPASoftAgcSimple, Debug)
				<< "Not adjusting exposure due to insufficient histogram data";
			return;
		}

		for (unsigned int i = 0; i < histogramSize; i++) {
			unsigned int idx = (i - (i / yHistValsPerBinMod)) / yHistValsPerBin;
			exposureBins[idx] += histogram[blackLevelHistIdx + i];
		}

		for (unsigned int i = 0; i < kExposureBinsCount; i++) {
			LOG(IPASoftAgcSimple, Debug) << i << ": " << exposureBins[i];
			denom += exposureBins[i];
			num += exposureBins[i] * (i + 1);
		}

		float exposureMSV = (denom == 0 ? 0 : static_cast<float>(num) / denom);
		updateExposure(session, state, frameContext, *params, exposureMSV);
		newExposureTime = state.automatic.exposure * session.lineDuration;
	}

	AgcAlgorithm::process(session, frameContext, newExposureTime, metadata);
}


} /* namespace ipa::soft::algorithms */

} /* namespace libcamera */
