/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * Copyright (C) 2024, Red Hat Inc.
 *
 * Color lookup tables construction
 */

#include "lut.h"

#include <algorithm>
#include <cmath>
#include <stdint.h>

#include <libcamera/base/log.h>

#include "simple/ipa_context.h"

namespace libcamera {

namespace ipa::soft::algorithms {

int Lut::configure(IPAContext &context,
		   [[maybe_unused]] const IPAConfigInfo &configInfo)
{
	/* Gamma value is fixed */
	context.configuration.gamma = 0.5;
	updateGammaTable(context);

	return 0;
}

void Lut::updateGammaTable(IPAContext &context)
{
	auto &gammaTable = context.activeState.gamma.gammaTable;
	auto blackLevel = context.activeState.blc.level;
	const unsigned int blackIndex = blackLevel * gammaTable.size() / 256;

	std::fill(gammaTable.begin(), gammaTable.begin() + blackIndex, 0);
	const float divisor = gammaTable.size() - blackIndex - 1.0;
	for (unsigned int i = blackIndex; i < gammaTable.size(); i++)
		gammaTable[i] = UINT8_MAX * std::pow((i - blackIndex) / divisor,
						     context.configuration.gamma);

	context.activeState.gamma.blackLevel = blackLevel;
}

int16_t Lut::ccmValue(unsigned int i, float ccm) const
{
	return std::round(i * ccm);
}

void Lut::prepare(IPAContext &context,
		  [[maybe_unused]] const uint32_t frame,
		  [[maybe_unused]] IPAFrameContext &frameContext,
		  DebayerParams *params)
{
	/*
	 * Update the gamma table if needed. This means if black level changes
	 * and since the black level gets updated only if a lower value is
	 * observed, it's not permanently prone to minor fluctuations or
	 * rounding errors.
	 */
	const bool gammaUpdateNeeded =
		context.activeState.gamma.blackLevel != context.activeState.blc.level;
	if (gammaUpdateNeeded)
		updateGammaTable(context);

	auto &gains = context.activeState.awb.gains;
	auto &gammaTable = context.activeState.gamma.gammaTable;
	const unsigned int gammaTableSize = gammaTable.size();
	const double div = static_cast<double>(DebayerParams::kRGBLookupSize) /
			   gammaTableSize;

	if (!context.activeState.ccm.enabled) {
		for (unsigned int i = 0; i < DebayerParams::kRGBLookupSize; i++) {
			/* Apply gamma after gain! */
			unsigned int idx;
			idx = std::min({ static_cast<unsigned int>(i * gains.red / div),
					 gammaTableSize - 1 });
			params->red.simple[i] = gammaTable[idx];
			idx = std::min({ static_cast<unsigned int>(i * gains.green / div),
					 gammaTableSize - 1 });
			params->green.simple[i] = gammaTable[idx];
			idx = std::min({ static_cast<unsigned int>(i * gains.blue / div),
					 gammaTableSize - 1 });
			params->blue.simple[i] = gammaTable[idx];
		}
	} else if (context.activeState.ccm.changed || gammaUpdateNeeded) {
		auto &ccm = context.activeState.ccm.ccm;
		auto &red = params->red.ccm;
		auto &green = params->green.ccm;
		auto &blue = params->blue.ccm;
		for (unsigned int i = 0; i < DebayerParams::kRGBLookupSize; i++) {
			red[i].r = ccmValue(i, ccm[0][0]);
			red[i].g = ccmValue(i, ccm[1][0]);
			red[i].b = ccmValue(i, ccm[2][0]);
			green[i].r = ccmValue(i, ccm[0][1]);
			green[i].g = ccmValue(i, ccm[1][1]);
			green[i].b = ccmValue(i, ccm[2][1]);
			blue[i].r = ccmValue(i, ccm[0][2]);
			blue[i].g = ccmValue(i, ccm[1][2]);
			blue[i].b = ccmValue(i, ccm[2][2]);
			params->gammaLut[i] = gammaTable[i / div];
		}
	}
}

REGISTER_IPA_ALGORITHM(Lut, "Lut")

} /* namespace ipa::soft::algorithms */

} /* namespace libcamera */
