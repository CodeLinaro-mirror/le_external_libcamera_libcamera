/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * Copyright (C) 2023-2025 Red Hat Inc.
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
	uint8_t blackLevel;
	float gamma;
	float contrast;
	RGB<float> gains;
	Matrix<float, 3, 3> combinedMatrix;
};

} /* namespace libcamera */
