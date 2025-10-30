/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (C) 2025, Ideas On Board Oy
 *
 * MetadataList tests
 */

#include <libcamera/control_ids.h>
#include <libcamera/metadata_list.h>
#include <libcamera/metadata_list_plan.h>
#include <libcamera/property_ids.h>

#include "test.h"

using namespace std;
using namespace libcamera;

class MetadataListIterUAFTest : public Test
{
public:
	MetadataListIterUAFTest() = default;

protected:
	int run() override
	{
		MetadataListPlan mlp;
		mlp.set(controls::AeEnable);

		MetadataList ml(mlp);
		std::ignore = *ml.begin(); /* Trigger ASAN. */

		return TestPass;
	}
};

TEST_REGISTER(MetadataListIterUAFTest)
