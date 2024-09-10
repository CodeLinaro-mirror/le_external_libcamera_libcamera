/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (C) 2019, Google Inc.
 *
 * ControlInfo tests
 */

#include <iostream>
#include <vector>

#include <libcamera/control_ids.h>
#include <libcamera/controls.h>

#include "test.h"

using namespace std;
using namespace libcamera;

class ControlInfoTest : public Test
{
protected:
	int run()
	{
		/*
		 * Test information retrieval from a range with no minimum and
		 * maximum.
		 */
		ControlInfo brightness;

		if (brightness.min().type() != ControlType::ControlTypeNone ||
		    brightness.max().type() != ControlType::ControlTypeNone ||
		    brightness.def().type() != ControlType::ControlTypeNone) {
			cout << "Invalid control range for Brightness" << endl;
			return TestFail;
		}

		/*
		 * Test information retrieval from a control with a minimum and
		 * a maximum value, and an implicit default value.
		 */
		ControlInfo contrast(10, 200);

		if (contrast.min().get<int32_t>() != 10 ||
		    contrast.max().get<int32_t>() != 200 ||
		    !contrast.def().isNone()) {
			cout << "Invalid control range for Contrast" << endl;
			return TestFail;
		}

		/*
		 * Test information retrieval from a control with boolean
		 * values.
		 */
		ControlInfo aeEnable({ false, true }, false);

		if (aeEnable.min().get<bool>() != false ||
		    aeEnable.def().get<bool>() != false ||
		    aeEnable.max().get<bool>() != true) {
			cout << "Invalid control range for AeEnable" << endl;
			return TestFail;
		}

		if (aeEnable.values()[0].get<bool>() != false ||
		    aeEnable.values()[1].get<bool>() != true) {
			cout << "Invalid control values for AeEnable" << endl;
			return TestFail;
		}

		ControlInfo awbEnable(true);

		if (awbEnable.min().get<bool>() != true ||
		    awbEnable.def().get<bool>() != true ||
		    awbEnable.max().get<bool>() != true) {
			cout << "Invalid control range for AwbEnable" << endl;
			return TestFail;
		}

		if (awbEnable.values()[0].get<bool>() != true) {
			cout << "Invalid control values for AwbEnable" << endl;
			return TestFail;
		}

		/*
		 * Test information retrieval from an enum control.
		 */
		ControlInfo awbMode(static_cast<int32_t>(controls::AwbTungsten),
				    static_cast<int32_t>(controls::AwbDaylight));
		if (awbMode.min().get<int32_t>() != controls::AwbTungsten ||
		    awbMode.max().get<int32_t>() != controls::AwbDaylight) {
			cout << "Invalid control range for AwbMode" << endl;
			return TestFail;
		}

		std::vector<ControlValue> modes = {
			static_cast<int32_t>(controls::AwbTungsten),
			static_cast<int32_t>(controls::AwbFluorescent),
			static_cast<int32_t>(controls::AwbDaylight),
		};
		ControlInfo awbModes(Span<const ControlValue>{ modes });

		if (awbModes.min() != modes.front() ||
		    awbModes.def() != modes.front() ||
		    awbModes.max() != modes.back()) {
			cout << "Invalid control range for AwbModes" << endl;
			return TestFail;
		}

		if (awbModes.values().size() != modes.size()) {
			cout << "Invalid size for AwbModes" << endl;
			return TestFail;
		}

		unsigned int i = 0;
		for (const auto &value : awbModes.values()) {
			if (value != modes.at(i++)) {
				cout << "Invalid control values for AwbModes" << endl;
				return TestFail;
			}
		}

		return TestPass;
	}
};

TEST_REGISTER(ControlInfoTest)
