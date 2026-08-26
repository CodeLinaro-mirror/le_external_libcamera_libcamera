/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * Copyright (C) 2026, Robert Bozik
 *
 * Temporal noise reduction parameters
 */

#include "denoise.h"

#include <algorithm>

#include <libcamera/base/log.h>

namespace libcamera {

LOG_DEFINE_CATEGORY(IPASoftIspDenoise)

namespace ipa::softisp::algorithms {

/*
 * Temporal noise reduction blends each raw frame with the previously
 * filtered one, before black level subtraction. The tuning file provides:
 *
 * - alpha: weight of the current frame, in ]0, 1]. Lower values average
 *   more frames (the noise standard deviation drops by roughly
 *   sqrt(alpha / (2 - alpha))) but react slower to changes; 1.0 disables
 *   the filter.
 * - noiseSlope, noiseFloor: sensor noise model at unity analogue gain,
 *   in normalised raw units: variance = noiseSlope * gain * signal +
 *   noiseFloor * gain^2, with the signal above the black level. The
 *   slope is the shot noise, the floor the read noise. Both are measured
 *   from the difference of consecutive frames of a static scene.
 * - motionSigma: difference between the current frame and the history,
 *   in sigmas of that noise, above which a pixel is considered to have
 *   moved and the current frame is used as is.
 */
static constexpr float kDefaultAlpha = 1.0f;
static constexpr float kDefaultMotionSigma = 3.0f;

int Denoise::init([[maybe_unused]] IPAContext &context, const ValueNode &tuningData)
{
	alpha_ = tuningData["alpha"].get<double>(kDefaultAlpha);
	noiseSlope_ = tuningData["noiseSlope"].get<double>(0.0);
	noiseFloor_ = tuningData["noiseFloor"].get<double>(0.0);
	motionSigma_ = tuningData["motionSigma"].get<double>(kDefaultMotionSigma);

	if (alpha_ <= 0.0f || alpha_ > 1.0f) {
		LOG(IPASoftIspDenoise, Error)
			<< "alpha must be in ]0, 1], got " << alpha_;
		return -EINVAL;
	}
	if (noiseSlope_ < 0.0f || noiseFloor_ < 0.0f || motionSigma_ <= 0.0f) {
		LOG(IPASoftIspDenoise, Error)
			<< "noiseSlope and noiseFloor must not be negative, "
			<< "motionSigma must be positive";
		return -EINVAL;
	}
	if (alpha_ < 1.0f && noiseSlope_ == 0.0f && noiseFloor_ == 0.0f) {
		LOG(IPASoftIspDenoise, Error)
			<< "A noise model (noiseSlope/noiseFloor) is required "
			<< "for temporal denoising";
		return -EINVAL;
	}

	LOG(IPASoftIspDenoise, Info)
		<< "Temporal denoise alpha " << alpha_
		<< " noise slope " << noiseSlope_ << " floor " << noiseFloor_
		<< " motion " << motionSigma_ << " sigma";

	return 0;
}

void Denoise::prepare(IPAContext &context, [[maybe_unused]] const uint32_t frame,
		      [[maybe_unused]] IPAFrameContext &frameContext,
		      DebayerParams *params)
{
	const double gain = std::max(context.activeState.agc.again, 1.0);

	params->temporalDenoise.alpha = alpha_;
	params->temporalDenoise.noiseSlope = noiseSlope_ * gain;
	params->temporalDenoise.noiseFloor = noiseFloor_ * gain * gain;
	params->temporalDenoise.motionSigma = motionSigma_;
}

REGISTER_IPA_ALGORITHM(Denoise, "Denoise")

} /* namespace ipa::softisp::algorithms */

} /* namespace libcamera */
