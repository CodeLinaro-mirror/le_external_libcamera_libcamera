/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * Copyright (C) 2026 Renesas Electronics Corp.
 * Copyright (C) 2026 Ideas on Board Oy
 * Copyright (C) 2026 Ragnatech AB
 *
 * RPP-X1 IPA Context
 */

#pragma once

#include <memory>

#include <linux/media/dreamchip/rppx1-config.h>

#include <libcamera/controls.h>

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

	std::unique_ptr<CameraSensorHelper> camHelper;
};

} /* namespace ipa::rppx1 */

} /* namespace libcamera*/
