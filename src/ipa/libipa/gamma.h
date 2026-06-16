/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * Copyright (C) 2026 Ideas on Board Oy
 *
 * libIPA Gamma correction algorithm
 */

#pragma once

#include <cmath>
#include <vector>

#include <libcamera/base/log.h>
#include <libcamera/base/span.h>

#include <libcamera/control_ids.h>

#include "libcamera/internal/value_node.h"

#include "fixedpoint.h"

namespace libcamera {

namespace ipa {

LOG_DECLARE_CATEGORY(Gamma)

namespace gamma {

struct ActiveState {
	double gamma;
};

struct FrameContext {
	double gamma;
	bool update;
};

} /* namespace gamma */

class GammaAlgorithmBase
{
public:
	GammaAlgorithmBase(unsigned int nLutNodes)
		: nLutNodes_(nLutNodes)
	{
	}

	int init(ControlInfoMap::Map &controls, const ValueNode &tuningData,
		 Span<unsigned int> segments = {});

	void configure(gamma::ActiveState &state);
	void queueRequest(gamma::ActiveState &state, const uint32_t frame,
			  gamma::FrameContext &context, const ControlList &controls);
	void process(gamma::FrameContext &context, ControlList &metadata);

protected:
	unsigned int nLutNodes_;
	float defaultGamma_;
	std::vector<unsigned int> segments_;
	unsigned int segmentsSum_;
};

template<unsigned int nLutNodes, typename UQ>
class GammaAlgorithm : public GammaAlgorithmBase
{
public:
	GammaAlgorithm()
		: GammaAlgorithmBase(nLutNodes)
	{
	}

	template<typename T>
	void prepare(gamma::FrameContext &context, Span<T> lut)
	{
		float x = 0;

		for (unsigned int i = 0; i < nLutNodes_; i++) {
			float gamma = std::pow(x / segmentsSum_,
					       1.0 / context.gamma);
			lut[i] = UQ(gamma).quantized();

			LOG(Gamma, Debug) << "LUT[" << i << "]=" << gamma << "(" << lut[i] << ")";

			if (i < segments_.size())
				x += segments_[i];
		}
	}
};

} /* namespace ipa */

} /* namespace libcamera */
