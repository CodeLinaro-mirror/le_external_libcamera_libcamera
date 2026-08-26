/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * Copyright (C) 2023-2026 Red Hat Inc.
 *
 * Authors:
 * Hans de Goede <hdegoede@redhat.com>
 *
 * DebayerParams header
 */

#pragma once

#include <stdint.h>

#include "libcamera/internal/matrix.h"
#include "libcamera/internal/vector.h"

namespace libcamera {

struct DebayerParams {
	Matrix<float, 3, 3> combinedMatrix = { { 1.0, 0.0, 0.0,
						 0.0, 1.0, 0.0,
						 0.0, 0.0, 1.0 } };
	RGB<double> blackLevel = RGB<double>({ 0.0, 0.0, 0.0 });
	float gamma = 1.0;
	float contrastExp = 1.0;
	RGB<double> gains = RGB<double>({ 1.0, 1.0, 1.0 });
	/*
	 * Temporal noise reduction of the raw data. alpha is the weight of
	 * the current frame (1.0 disables the filter). Motion is detected
	 * where the frame differs from the history by more than motionSigma
	 * times the expected noise, whose variance in normalised raw units is
	 * noiseSlope * signal + noiseFloor (signal above black level, already
	 * scaled for the current analogue gain).
	 */
	struct {
		float alpha = 1.0;
		float noiseSlope = 0.0;
		float noiseFloor = 0.0;
		float motionSigma = 0.0;
	} temporalDenoise;
};

} /* namespace libcamera */
