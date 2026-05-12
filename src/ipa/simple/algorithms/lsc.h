/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * Lens shading correction
 */

#pragma once

#include "libcamera/internal/matrix.h"
#include "libcamera/internal/vector.h"

#include <libipa/interpolator.h>

#include "algorithm.h"

namespace libcamera {

namespace ipa::soft::algorithms {

class Lsc : public Algorithm
{
public:
	Lsc() = default;
	~Lsc() = default;

	int init(IPAContext &context, const ValueNode &tuningData) override;
	int configure(IPAContext &context,
		      const IPAConfigInfo &configInfo) override;
	void prepare(IPAContext &context,
		     const uint32_t frame,
		     IPAFrameContext &frameContext,
		     DebayerParams *params) override;

private:
	using LscMatrix = Matrix<float, DebayerParams::kLscGridSize, DebayerParams::kLscGridSize>;
	DebayerParams::LscType type_;
	Interpolator<LscMatrix> lscR_;
	Interpolator<LscMatrix> lscG_;
	Interpolator<LscMatrix> lscB_;
	Interpolator<Vector<float, DebayerParams::kNLscCoefficients>> lscCoefR_;
	Interpolator<Vector<float, DebayerParams::kNLscCoefficients>> lscCoefG_;
	Interpolator<Vector<float, DebayerParams::kNLscCoefficients>> lscCoefB_;
};

} /* namespace ipa::soft::algorithms */

} /* namespace libcamera */
