/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (C) 2024, Ideas on Board Oy.
 */

#include "libipa/camera_sensor_helper.h"

#include <iostream>
#include <string.h>

#include "test.h"

using namespace std;
using namespace libcamera;
using namespace libcamera::ipa;

class CameraSensorHelperTest : public Test
{
protected:
	int testGainModel(std::string model)
	{
		int ret = TestPass;

		std::unique_ptr<CameraSensorHelper> camHelper_;

		camHelper_ = CameraSensorHelperFactoryBase::create(model);
		if (!camHelper_) {
			std::cout
				<< "Failed to create camera sensor helper for "
				<< model;
			return TestFail;
		}

		for (unsigned int i = 0; i < 240; i++) {
			float gain = camHelper_->gain(i);
			uint32_t gainCode = camHelper_->gainCode(gain);

			if (i != gainCode) {
				std::cout << model << ": Gain conversions failed: "
					  << i << " : " << gain << " : "
					  << gainCode << std::endl;

				ret = TestFail;
			}
		};

		return ret;
	}

	int run() override
	{
		unsigned int failures = 0;

		std::vector<CameraSensorHelperFactoryBase *> factories;

		for (auto factory : CameraSensorHelperFactoryBase::factories()) {
			const std::string &model = factory->name();

			cout << "Testing CameraSensorHelper for " << model << endl;

			if (testGainModel(factory->name()) == TestFail)
				failures++;
		}

		return failures ? TestFail : TestPass;
	}
};

TEST_REGISTER(CameraSensorHelperTest)
