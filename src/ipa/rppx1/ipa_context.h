/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * Copyright (C) 2026, Ideas On Board
 *
 * RPP-X1 IPA Context
 */

#pragma once

#include <memory>

#include <libcamera/base/utils.h>

#include <libcamera/control_ids.h>
#include <libcamera/controls.h>
#include <libcamera/geometry.h>

#include <libcamera/ipa/core_ipa_interface.h>

#include <libipa/camera_sensor_helper.h>
#include <libipa/fc_queue.h>

namespace libcamera {

namespace ipa::rppx1 {

struct IPASessionConfiguration {
};

struct IPAActiveState {
};

struct IPAFrameContext : public FrameContext {
};

struct IPAContext {
	IPAContext(unsigned int frameContextSize)
		: frameContexts(frameContextSize)
	{
	}

	IPACameraSensorInfo sensorInfo;
	IPASessionConfiguration configuration;
	IPAActiveState activeState;

	FCQueue<IPAFrameContext> frameContexts;

	ControlInfoMap::Map ctrlMap;

	ControlInfoMap sensorControls;

	/* Interface to the Camera Helper */
	std::unique_ptr<CameraSensorHelper> camHelper;
};

} /* namespace ipa::rppx1 */

} /* namespace libcamera*/
