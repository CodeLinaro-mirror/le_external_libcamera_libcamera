/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (C) 2026, James Alexander
 *
 * Soft ISP image adjustment algorithm tests
 */

#include <cmath>
#include <errno.h>
#include <iostream>
#include <memory>

#include <libcamera/control_ids.h>

#include "libcamera/internal/value_node.h"

#include "algorithms/adjust.h"

#include "test.h"

using namespace libcamera;
using namespace libcamera::ipa::softisp;
using namespace libcamera::ipa::softisp::algorithms;

namespace {

constexpr float kEpsilon = 0.0001f;

bool closeEnough(float lhs, float rhs)
{
	return std::abs(lhs - rhs) < kEpsilon;
}

} /* namespace */

class AdjustTest : public Test
{
protected:
	int testDefaults()
	{
		IPAContext context(1);
		ValueNode tuning;
		Adjust adjust;

		if (adjust.init(context, tuning))
			return TestFail;

		IPAConfigInfo configInfo{};
		if (adjust.configure(context, configInfo))
			return TestFail;

		const float contrast = context.ctrlMap.at(&controls::Contrast).def().get<float>();
		if (!closeEnough(contrast, 1.0f) ||
		    !context.activeState.knobs.contrast ||
		    !closeEnough(*context.activeState.knobs.contrast, 1.0f)) {
			std::cerr << "Default contrast was not preserved" << std::endl;
			return TestFail;
		}

		return TestPass;
	}

	int testTunedDefaultAndControl()
	{
		IPAContext context(1);
		ValueNode tuning;
		tuning.add("contrast", std::make_unique<ValueNode>(1.15f));
		Adjust adjust;

		if (adjust.init(context, tuning))
			return TestFail;

		IPAConfigInfo configInfo{};
		if (adjust.configure(context, configInfo))
			return TestFail;

		const float contrast = context.ctrlMap.at(&controls::Contrast).def().get<float>();
		if (!closeEnough(contrast, 1.15f) ||
		    !context.activeState.knobs.contrast ||
		    !closeEnough(*context.activeState.knobs.contrast, 1.15f)) {
			std::cerr << "Tuned contrast default was not applied" << std::endl;
			return TestFail;
		}

		ControlList controlsList(controls::controls);
		controlsList.set(controls::Contrast, 0.9f);
		IPAFrameContext frameContext{};
		adjust.queueRequest(context, 0, frameContext, controlsList);

		if (!context.activeState.knobs.contrast ||
		    !closeEnough(*context.activeState.knobs.contrast, 0.9f)) {
			std::cerr << "Request control did not override tuned contrast" << std::endl;
			return TestFail;
		}

		return TestPass;
	}

	int testInvalidTuning()
	{
		IPAContext context(1);
		ValueNode tuning;
		tuning.add("contrast", std::make_unique<ValueNode>(3.0f));
		Adjust adjust;

		if (adjust.init(context, tuning) != -EINVAL) {
			std::cerr << "Out-of-range contrast tuning was accepted" << std::endl;
			return TestFail;
		}

		return TestPass;
	}

	int run() override
	{
		if (testDefaults() != TestPass ||
		    testTunedDefaultAndControl() != TestPass ||
		    testInvalidTuning() != TestPass)
			return TestFail;

		return TestPass;
	}
};

TEST_REGISTER(AdjustTest)
