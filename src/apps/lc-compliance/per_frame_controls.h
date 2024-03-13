/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (C) 2024, Ideas on Board Oy
 *
 * per_frame_controls.h - Tests for per frame controls
 */

#pragma once

#include <memory>

#include <libcamera/libcamera.h>

#include "../common/event_loop.h"

#include "simple_capture.h"
#include "time_sheet.h"

class PerFrameControls : public SimpleCapture
{
public:
	PerFrameControls(std::shared_ptr<libcamera::Camera> camera);

	std::shared_ptr<TimeSheet>
	startCaptureWithTimeSheet(unsigned int framesToCapture, const libcamera::ControlList *controls = nullptr);
	void runCaptureSession();

	void testExposureGainChangeOnSameFrame();
	void testFramePreciseExposureChange();
	void testFramePreciseGainChange();
	void testExposureGainIsAppliedOnFirstFrame();
	void testExposureGainFromFirstRequestGetsApplied();
	void testExposureGainFromFirstAndSecondRequestGetsApplied();

	int queueRequest(libcamera::Request *request);
	void requestComplete(libcamera::Request *request) override;

	unsigned int queueCount_;
	unsigned int captureCount_;
	unsigned int captureLimit_;

	std::weak_ptr<TimeSheet> timeSheet_;
};
