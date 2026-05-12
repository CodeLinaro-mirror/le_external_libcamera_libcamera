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

int Lsc::init(IPAContext &context, const ValueNode &tuningData)
{
	std::string type = tuningData["type"].get<std::string>("table");

	if (type == "table") {
		int retR = lscR_.readYaml(tuningData["sets"], "ct", "r");
		int retG = lscG_.readYaml(tuningData["sets"], "ct", "g");
		int retB = lscB_.readYaml(tuningData["sets"], "ct", "b");

		if (retR < 0 || retG < 0 || retB < 0) {
			LOG(IPASoftLsc, Error)
				<< "Failed to parse 'lsc' parameter from tuning file.";
			return -EINVAL;
		}

		type_ = DebayerParams::LscTable;
	} else {
		LOG(IPASoftLsc, Error) << "LSC: type " << type << " not supported";
		return -EINVAL;
	}

	context.lscEnabled = true;

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

	const LscMatrix matrixR = lscR_.getInterpolated(ct);
	const LscMatrix matrixG = lscG_.getInterpolated(ct);
	const LscMatrix matrixB = lscB_.getInterpolated(ct);

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
