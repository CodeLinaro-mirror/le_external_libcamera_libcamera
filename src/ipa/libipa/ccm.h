/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * Copyright (C) 2026 Ideas on Board Oy
 *
 * libIPA CCM algorithm
 */

#pragma once

#include <libcamera/control_ids.h>
#include <libcamera/controls.h>

#include "libcamera/internal/matrix.h"

#include "fixedpoint.h"
#include "interpolator.h"

namespace libcamera {

namespace ipa {

namespace ccm {

struct ActiveState {
	struct CcmState {
		Matrix<float, 3, 3> ccm;
		Matrix<int16_t, 3, 1> offsets;
	};

	struct CcmState manual;
	struct CcmState automatic;
};

struct FrameContext {
	Matrix<float, 3, 3> ccm;
	Matrix<int16_t, 3, 1> offsets;
};

} /* namespace ccm */

class CcmAlgorithmBase
{
public:
	int init(const ValueNode &tuningData);
	int configure(ccm::ActiveState &state, unsigned int temperatureK);
	void queueRequest(ccm::ActiveState &state, ccm::FrameContext &context,
			  const ControlList &controls);

	void prepare(ccm::ActiveState &state, ccm::FrameContext &context,
		     unsigned int frame, unsigned int temperatureK);
	void process(ccm::FrameContext &context, ControlList &metadata);

protected:
	float coeffMin_;
	float coeffMax_;

private:
	unsigned int ct_;
	Interpolator<Matrix<float, 3, 3>> ccm_;
	Interpolator<Matrix<int16_t, 3, 1>> offsets_;
};

template<typename Q>
class CcmAlgorithm : public CcmAlgorithmBase
{
public:
	int init(const ValueNode &tuningData, ControlInfoMap::Map &controls)
	{
		int ret = CcmAlgorithmBase::init(tuningData);
		if (ret)
			return ret;

		coeffMin_ = Q::TraitsType::min;
		coeffMax_ = Q::TraitsType::max;

		controls[&controls::ColourCorrectionMatrix] =
			ControlInfo(ControlValue(coeffMin_),
				    ControlValue(coeffMax_),
				    ControlValue(Matrix<float, 3, 3>::identity().data()));

		return 0;
	}
};

} /* namespace ipa */

} /* namespace libcamera */
