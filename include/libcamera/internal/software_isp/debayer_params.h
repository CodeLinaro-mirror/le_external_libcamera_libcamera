/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * Copyright (C) 2023, 2024 Red Hat Inc.
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

	struct CcmRow {
		int16_t c1;
		int16_t c2;
		int16_t c3;
	};

	using ColorLookupTable = std::array<uint8_t, kRGBLookupSize>;
	using CcmLookupTable = std::array<CcmRow, kRGBLookupSize>;
	using GammaLookupTable = std::array<uint8_t, kRGBLookupSize>;

	union LookupTable {
		ColorLookupTable simple;
		CcmLookupTable ccm;
	};

	LookupTable red;
	LookupTable green;
	LookupTable blue;
	GammaLookupTable gammaLut;
};

} /* namespace libcamera */
