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

#include <array>
#include <stdint.h>

namespace libcamera {

struct DebayerParams {
	static constexpr unsigned int kRGBLookupSize = 256;

	struct CcmColumn {
		int16_t r;
		int16_t g;
		int16_t b;
	};

	using ColorLookupTable = std::array<uint8_t, kRGBLookupSize>;
	using CcmLookupTable = std::array<CcmColumn, kRGBLookupSize>;
	using GammaLookupTable = std::array<uint8_t, kRGBLookupSize>;

	ColorLookupTable red;
	ColorLookupTable green;
	ColorLookupTable blue;

	CcmLookupTable redCcm;
	CcmLookupTable greenCcm;
	CcmLookupTable blueCcm;
	GammaLookupTable gammaLut;
};

} /* namespace libcamera */
