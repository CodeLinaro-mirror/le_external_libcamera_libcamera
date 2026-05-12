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
	int retR, retG, retB;

	if (type == "table") {
		retR = lscR_.readYaml(tuningData["sets"], "ct", "r");
		retG = lscG_.readYaml(tuningData["sets"], "ct", "g");
		retB = lscB_.readYaml(tuningData["sets"], "ct", "b");
		type_ = DebayerParams::LscTable;
	} else if (type == "polynomial") {
		retR = lscCoefR_.readYaml(tuningData["sets"], "ct", "r");
		retG = lscCoefG_.readYaml(tuningData["sets"], "ct", "g");
		retB = lscCoefB_.readYaml(tuningData["sets"], "ct", "b");
		type_ = DebayerParams::LscPolynomial;
	} else {
		LOG(IPASoftLsc, Error) << "LSC: type " << type << " not supported";
		return -EINVAL;
	}
	if (retR < 0 || retG < 0 || retB < 0) {
		LOG(IPASoftLsc, Error)
			<< "Failed to parse 'lsc' parameter from tuning file.";
		return -EINVAL;
	}

	context.lscType = type_;

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

	switch (type_) {
	case DebayerParams::LscNone:
		break;

	case DebayerParams::LscTable: {
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

		break;
	}

	case DebayerParams::LscPolynomial: {
		const Vector<float, DebayerParams::kNLscCoefficients> coefR =
			lscCoefR_.getInterpolated(ct);
		const Vector<float, DebayerParams::kNLscCoefficients> coefG =
			lscCoefG_.getInterpolated(ct);
		const Vector<float, DebayerParams::kNLscCoefficients> coefB =
			lscCoefB_.getInterpolated(ct);

		for (unsigned int i = 0; i < DebayerParams::kNLscCoefficients; i++) {
			params->lscCoefficients[i].r() = coefR[i];
			params->lscCoefficients[i].g() = coefG[i];
			params->lscCoefficients[i].b() = coefB[i];
		}
		break;
	}
	}
}

REGISTER_IPA_ALGORITHM(Lsc, "Lsc")

} /* namespace ipa::soft::algorithms */

} /* namespace libcamera */
