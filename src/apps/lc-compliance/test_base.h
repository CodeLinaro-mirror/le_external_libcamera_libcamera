/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (C) 2021, Collabora Ltd.
 *
 * test_base.h - Base definitions for tests
 */
#pragma once

#include <libcamera/libcamera.h>

#include <gtest/gtest.h>

class CameraHolder
{
protected:
	void acquireCamera();
	void releaseCamera();

	std::shared_ptr<libcamera::Camera> camera_;
};
