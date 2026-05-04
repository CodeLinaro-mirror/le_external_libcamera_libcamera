/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * Lens shading correction
 */

#include "lsc.h"

#include <libcamera/base/log.h>

#include "awb.h"

namespace libcamera {

namespace ipa::soft::algorithms {

LOG_DEFINE_CATEGORY(IPASoftLsc)

int Lsc::init([[maybe_unused]] IPAContext &context, const ValueNode &tuningData)
{
	int ret_r = lscR.readYaml(tuningData["grids"], "ct", "r");
	int ret_g = lscG.readYaml(tuningData["grids"], "ct", "g");
	int ret_b = lscB.readYaml(tuningData["grids"], "ct", "b");

	if (ret_r < 0 || ret_g < 0 || ret_b < 0) {
		LOG(IPASoftLsc, Error)
			<< "Failed to parse 'lsc' parameter from tuning file.";
		return -EINVAL;
	}

	return 0;
}

int Lsc::configure([[maybe_unused]] IPAContext &context,
		   [[maybe_unused]] const IPAConfigInfo &configInfo)
{
	return 0;
}

void Lsc::prepare(IPAContext &context, [[maybe_unused]] const uint32_t frame,
		  [[maybe_unused]] IPAFrameContext &frameContext, DebayerParams *params)
{
	unsigned int ct =
		context.activeState.awb.temperatureK.value_or(kDefaultTemperature);

	const LscMatrix matrixR = lscR.getInterpolated(ct);
	const LscMatrix matrixG = lscG.getInterpolated(ct);
	const LscMatrix matrixB = lscB.getInterpolated(ct);

	DebayerParams::LscLookupTable lut;
	constexpr unsigned int gridSize = DebayerParams::kLscGridSize;
	for (unsigned int i = 0, j = 0; i < gridSize * gridSize; i++) {
		lut[j++] = matrixR.data()[i];
		lut[j++] = matrixG.data()[i];
		lut[j++] = matrixB.data()[i];
	}
	params->lscLut = lut;
}

REGISTER_IPA_ALGORITHM(Lsc, "Lsc")

} /* namespace ipa::soft::algorithms */

} /* namespace libcamera */
