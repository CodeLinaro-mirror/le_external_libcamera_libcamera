/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (C) 2021, Collabora Ltd.
 *
 * property_test.cpp - Test camera properties
 */

#include <libcamera/libcamera.h>

#include <gtest/gtest.h>

#include "test_base.h"

using namespace libcamera;

TEST_F(CameraTests, RequiredProperties)
{
	const ControlList &properties = camera_->properties();

	using namespace properties;

	EXPECT_GT(properties.get(MinimumRequests), 0)
		<< "Camera should have a positive value for MinimumRequests property";
}
