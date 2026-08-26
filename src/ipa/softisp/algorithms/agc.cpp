/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * Copyright (C) 2024, Red Hat Inc.
 *
 * Exposure and gain
 */

#include "agc.h"

#include <algorithm>
#include <cmath>
#include <stdint.h>

#include <libcamera/base/log.h>
#include <libcamera/base/utils.h>

#include <libcamera/control_ids.h>

#include "control_ids.h"

namespace libcamera {

LOG_DEFINE_CATEGORY(IPASoftIspExposure)

using namespace std::literals::chrono_literals;

namespace ipa::softisp::algorithms {

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

/*
 * Errors above this threshold are far from the target, and the small
 * proportional steps would need tens of statistics periods to get there
 * (a statistics period being several frames, and the frame possibly long
 * in low light). For those, jump by the ratio between the target and the
 * measured MSV instead, bounded by kExpMaxJump per step. The proportional
 * correction takes over near the target, so the convergence stays smooth.
 */
static constexpr float kExpLargeError = 0.5;
static constexpr float kExpMaxJump = 2.0;

/*
 * Digital gain is applied by the ISP on top of the sensor exposure and
 * analogue gain, only when those are exhausted. It doesn't add information,
 * so the tuning file bounds it with maxDigitalGain (1.0 disables it).
 */
static constexpr double kDefaultMaxDigitalGain = 1.0;

Agc::Agc()
{
}

int Agc::init(IPAContext &context, const ValueNode &tuningData)
{
	maxDigitalGain_ = tuningData["maxDigitalGain"].get<double>(kDefaultMaxDigitalGain);
	if (maxDigitalGain_ < 1.0) {
		LOG(IPASoftIspExposure, Warning)
			<< "maxDigitalGain " << maxDigitalGain_ << " below 1.0, ignored";
		maxDigitalGain_ = 1.0;
	}

	/*
	 * Expose the frame duration limits the sensor can achieve in the
	 * current mode. Whether the IPA can actually change the frame duration
	 * is only known in configure(), when the sensor controls are available.
	 */
	const IPACameraSensorInfo &sensorInfo = context.sensorInfo;
	if (!sensorInfo.pixelRate || !sensorInfo.minLineLength) {
		LOG(IPASoftIspExposure, Warning)
			<< "Missing sensor timing information, "
			<< "FrameDurationLimits not exposed";
		return 0;
	}

	utils::Duration lineDuration = sensorInfo.minLineLength * 1.0s / sensorInfo.pixelRate;
	utils::Duration minDuration = lineDuration * sensorInfo.minFrameLength;
	utils::Duration maxDuration = lineDuration * sensorInfo.maxFrameLength;
	int64_t minFrameDuration = minDuration.get<std::micro>();
	int64_t maxFrameDuration = maxDuration.get<std::micro>();

	context.ctrlMap[&controls::FrameDurationLimits] =
		ControlInfo(minFrameDuration, maxFrameDuration, minFrameDuration);

	return 0;
}

int Agc::configure(IPAContext &context, [[maybe_unused]] const IPAConfigInfo &configInfo)
{
	auto &agc = context.activeState.agc;
	const auto &cfg = context.configuration.agc;

	/*
	 * Default to the full range the sensor supports, applications restrict
	 * it through FrameDurationLimits. Without vblank control the frame
	 * duration is fixed at the sensor default.
	 */
	const auto it = context.ctrlMap.find(&controls::FrameDurationLimits);
	if (it != context.ctrlMap.end() && cfg.vblankSupported) {
		agc.minFrameDuration = std::chrono::microseconds(it->second.min().get<int64_t>());
		agc.maxFrameDuration = std::chrono::microseconds(it->second.max().get<int64_t>());
	} else {
		agc.minFrameDuration = cfg.lineDuration * (cfg.frameHeight + cfg.vblankDef);
		agc.maxFrameDuration = agc.minFrameDuration;
	}
	agc.vblank = cfg.vblankDef;
	agc.dgain = 1.0;
	context.configuration.agc.dgainMax = maxDigitalGain_;

	return 0;
}

void Agc::prepare(IPAContext &context, [[maybe_unused]] const uint32_t frame,
		  IPAFrameContext &frameContext, DebayerParams *params)
{
	/*
	 * The digital gain scales the colour gains the ISP applies after black
	 * level subtraction. This runs after the AWB has set the gains, as the
	 * Agc algorithm is listed after Awb in the tuning file.
	 */
	frameContext.agc.digitalGain = context.activeState.agc.dgain;
	params->gains *= frameContext.agc.digitalGain;
}

void Agc::queueRequest(IPAContext &context, [[maybe_unused]] const uint32_t frame,
		       IPAFrameContext &frameContext, const ControlList &controls)
{
	auto &agc = context.activeState.agc;

	const auto &frameDurationLimits = controls.get(controls::FrameDurationLimits);
	if (frameDurationLimits && context.configuration.agc.vblankSupported) {
		const auto it = context.ctrlMap.find(&controls::FrameDurationLimits);
		if (it != context.ctrlMap.end()) {
			const ControlInfo &limits = it->second;
			int64_t minFrameDuration =
				std::clamp((*frameDurationLimits).front(),
					   limits.min().get<int64_t>(),
					   limits.max().get<int64_t>());
			int64_t maxFrameDuration =
				std::clamp((*frameDurationLimits).back(),
					   limits.min().get<int64_t>(),
					   limits.max().get<int64_t>());
			if (maxFrameDuration < minFrameDuration)
				maxFrameDuration = minFrameDuration;

			agc.minFrameDuration = std::chrono::microseconds(minFrameDuration);
			agc.maxFrameDuration = std::chrono::microseconds(maxFrameDuration);
		}
	}

	frameContext.agc.minFrameDuration = agc.minFrameDuration;
	frameContext.agc.maxFrameDuration = agc.maxFrameDuration;
}

/*
 * Translate the frame duration limits of the frame into a vblank range,
 * clamped to what the sensor supports.
 */
void Agc::vblankRange(const IPAContext &context, const IPAFrameContext &frameContext,
		      int32_t &vblankLo, int32_t &vblankHi) const
{
	const auto &cfg = context.configuration.agc;

	if (!cfg.vblankSupported) {
		vblankLo = vblankHi = cfg.vblankDef;
		return;
	}

	/*
	 * The limits are expressed in microseconds, which can't represent the
	 * line timing exactly. Round to the nearest line, so that a limit
	 * derived from a whole number of lines maps back to that number.
	 */
	const double minLines = std::round(frameContext.agc.minFrameDuration / cfg.lineDuration);
	const double maxLines = std::round(frameContext.agc.maxFrameDuration / cfg.lineDuration);
	const int64_t height = cfg.frameHeight;

	vblankLo = static_cast<int32_t>(std::clamp<int64_t>(
		static_cast<int64_t>(minLines) - height, cfg.vblankMin, cfg.vblankMax));
	vblankHi = static_cast<int32_t>(std::clamp<int64_t>(
		static_cast<int64_t>(maxLines) - height, cfg.vblankMin, cfg.vblankMax));
	if (vblankHi < vblankLo)
		vblankHi = vblankLo;
}

/*
 * Maximum exposure the sensor accepts for a given vblank. The driver keeps
 * exposureMargin lines between the exposure and the frame length.
 */
int32_t Agc::exposureMaxForVblank(const IPAContext &context, int32_t vblank) const
{
	const auto &cfg = context.configuration.agc;

	if (!cfg.vblankSupported)
		return cfg.exposureMax;

	return std::max(cfg.exposureMin,
			static_cast<int32_t>(cfg.frameHeight) + vblank - cfg.exposureMargin);
}

void Agc::updateExposure(IPAContext &context, IPAFrameContext &frameContext, double exposureMSV)
{
	int32_t &exposure = frameContext.sensor.exposure;
	double &again = frameContext.sensor.gain;
	int32_t &vblank = frameContext.sensor.vblank;
	double &dgain = frameContext.agc.digitalGain;
	const double dgainMax = context.configuration.agc.dgainMax;

	int32_t vblankLo, vblankHi;
	vblankRange(context, frameContext, vblankLo, vblankHi);

	/*
	 * The exposure may grow up to what the longest allowed frame permits;
	 * the vblank then follows the exposure, so the frame is only made
	 * longer when the exposure needs it.
	 */
	const int32_t exposureMax = exposureMaxForVblank(context, vblankHi);

	double error = kExposureOptimal - exposureMSV;

	if (std::abs(error) <= kExposureSatisfactory) {
		/* Still honour changed frame duration limits. */
		exposure = std::clamp(exposure, context.configuration.agc.exposureMin,
				      exposureMax);
		vblank = std::clamp(exposure + context.configuration.agc.exposureMargin -
					    static_cast<int32_t>(context.configuration.agc.frameHeight),
				    vblankLo, vblankHi);
		context.activeState.agc.exposure = exposure;
		context.activeState.agc.vblank = vblank;
		return;
	}

	/*
	 * Compute the correction factor. The sign of the error determines the
	 * direction: positive error means too dark (increase), negative means
	 * too bright (decrease). Far from the target, jump by the measured
	 * ratio; near it, apply a small proportional step.
	 */
	float factor;
	if (std::abs(error) > kExpLargeError) {
		factor = std::clamp(static_cast<float>(kExposureOptimal / std::max(exposureMSV, 0.1)),
				    1.0f / kExpMaxJump, kExpMaxJump);
	} else {
		float step = std::clamp(static_cast<float>(error) * kExpProportionalGain,
					-kExpMaxStep, kExpMaxStep);
		factor = 1.0f + step;
	}

	if (factor > 1.0f) {
		/*
		 * Scene too dark: increase exposure first (lengthening the
		 * frame when the limits allow it), then analogue gain, then
		 * digital gain.
		 */
		if (exposure < exposureMax) {
			int32_t next = static_cast<int32_t>(exposure * factor);
			exposure = std::max(next, exposure + 1);
		} else if (again < context.configuration.agc.againMax) {
			double next = again * factor;
			if (next - again < context.configuration.agc.againMinStep)
				again += context.configuration.agc.againMinStep;
			else
				again = next;
		} else {
			dgain = std::min(dgain * factor, dgainMax);
		}
	} else {
		/*
		 * Scene too bright: decrease digital gain first, then analogue
		 * gain, then exposure.
		 */
		if (dgain > 1.0) {
			dgain = std::max(dgain * factor, 1.0);
		} else if (again > context.configuration.agc.again10) {
			double next = again * factor;
			if (again - next < context.configuration.agc.againMinStep)
				again -= context.configuration.agc.againMinStep;
			else
				again = next;
		} else {
			int32_t next = static_cast<int32_t>(exposure * factor);
			exposure = std::min(next, exposure - 1);
		}
	}

	exposure = std::clamp(exposure, context.configuration.agc.exposureMin,
			      exposureMax);
	again = std::clamp(again, context.configuration.agc.againMin,
			   context.configuration.agc.againMax);
	vblank = std::clamp(exposure + context.configuration.agc.exposureMargin -
				    static_cast<int32_t>(context.configuration.agc.frameHeight),
			    vblankLo, vblankHi);

	dgain = std::clamp(dgain, 1.0, dgainMax);

	context.activeState.agc.exposure = exposure;
	context.activeState.agc.again = again;
	context.activeState.agc.vblank = vblank;
	context.activeState.agc.dgain = dgain;

	LOG(IPASoftIspExposure, Debug)
		<< "exposureMSV " << exposureMSV
		<< " error " << error << " factor " << factor
		<< " exp " << exposure << " again " << again
		<< " dgain " << dgain
		<< " vblank " << vblank << " (" << vblankLo << "-" << vblankHi << ")";
}

void Agc::process(IPAContext &context,
		  [[maybe_unused]] const uint32_t frame,
		  IPAFrameContext &frameContext,
		  const SwIspStats *stats,
		  ControlList &metadata)
{
	const auto &cfg = context.configuration.agc;
	utils::Duration exposureTime = cfg.lineDuration * frameContext.sensor.exposure;
	metadata.set(controls::ExposureTime, exposureTime.get<std::micro>());
	metadata.set(controls::AnalogueGain, frameContext.sensor.gain);
	metadata.set(controls::DigitalGain, static_cast<float>(frameContext.agc.digitalGain));
	if (cfg.vblankSupported) {
		frameContext.agc.frameDuration =
			cfg.lineDuration * (cfg.frameHeight + frameContext.sensor.vblank);
		metadata.set(controls::FrameDuration,
			     frameContext.agc.frameDuration.get<std::micro>());
	}

	if (!context.activeState.agc.valid) {
		/*
		 * Init active-state from sensor values in case updateExposure()
		 * does not run for the first frame.
		 */
		context.activeState.agc.exposure = frameContext.sensor.exposure;
		context.activeState.agc.again = frameContext.sensor.gain;
		context.activeState.agc.vblank = frameContext.sensor.vblank;
		context.activeState.agc.valid = true;
	}

	if (!stats->valid) {
		/*
		 * Use the new exposure and gain values calculated the last time
		 * there were valid stats.
		 */
		frameContext.sensor.exposure = context.activeState.agc.exposure;
		frameContext.sensor.gain = context.activeState.agc.again;
		frameContext.sensor.vblank = context.activeState.agc.vblank;
		frameContext.agc.digitalGain = context.activeState.agc.dgain;
		return;
	}

	/*
	 * Calculate Mean Sample Value (MSV) according to formula from:
	 * https://www.araa.asn.au/acra/acra2007/papers/paper84final.pdf
	 */
	const auto &histogram = stats->yHistogram;
	const unsigned int blackLevelHistIdx =
		context.activeState.blc.level / (256 / SwIspStats::kYHistogramSize);
	const unsigned int histogramSize =
		SwIspStats::kYHistogramSize - blackLevelHistIdx;
	const unsigned int yHistValsPerBin = histogramSize / kExposureBinsCount;
	const unsigned int yHistValsPerBinMod =
		histogramSize / (histogramSize % kExposureBinsCount + 1);
	int exposureBins[kExposureBinsCount] = {};
	unsigned int denom = 0;
	unsigned int num = 0;

	if (yHistValsPerBin == 0) {
		LOG(IPASoftIspExposure, Debug)
			<< "Not adjusting exposure due to insufficient histogram data";
		return;
	}

	/*
	 * The statistics are computed on the sensor data, before the ISP
	 * applies the digital gain. Scale the histogram index by the digital
	 * gain of the frame so that the MSV reflects the output brightness.
	 */
	const double digitalGain = frameContext.agc.digitalGain;
	for (unsigned int i = 0; i < histogramSize; i++) {
		unsigned int scaled = std::min<unsigned int>(
			static_cast<unsigned int>(i * digitalGain), histogramSize - 1);
		unsigned int idx = (scaled - (scaled / yHistValsPerBinMod)) / yHistValsPerBin;
		exposureBins[idx] += histogram[blackLevelHistIdx + i];
	}

	for (unsigned int i = 0; i < kExposureBinsCount; i++) {
		LOG(IPASoftIspExposure, Debug) << i << ": " << exposureBins[i];
		denom += exposureBins[i];
		num += exposureBins[i] * (i + 1);
	}

	float exposureMSV = (denom == 0 ? 0 : static_cast<float>(num) / denom);
	updateExposure(context, frameContext, exposureMSV);
}

REGISTER_IPA_ALGORITHM(Agc, "Agc")

} /* namespace ipa::softisp::algorithms */

} /* namespace libcamera */
