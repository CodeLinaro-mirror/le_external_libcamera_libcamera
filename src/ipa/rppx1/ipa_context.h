/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * Copyright (C) 2026, Ideas On Board
 *
 * RPP-X1 IPA Context
 */

#pragma once

#include <memory>

#include <linux/media/dreamchip/rppx1-config.h>

#include <libcamera/control_ids.h>
#include <libcamera/controls.h>

#include <libcamera/ipa/core_ipa_interface.h>

#include <libipa/agc.h>
#include <libipa/awb.h>
#include <libipa/ccm.h>
#include <libipa/camera_sensor_helper.h>
#include <libipa/fc_queue.h>
#include <libipa/gamma.h>

namespace libcamera {

namespace ipa::rppx1 {

struct IPASessionConfiguration {
	struct Agc : ipa::agc::Session {
		rppx1_window measureWindow;
	} agc;

	struct {
		struct rppx1_window measureWindow;
		bool enabled;
	} awb;
};

struct IPAActiveState {
	struct Agc : ipa::agc::ActiveState {
		controls::AeMeteringModeEnum meteringMode;
	} agc;

	ipa::awb::ActiveState awb;

	ipa::ccm::ActiveState ccm;

	ipa::gamma::ActiveState goc;

	struct {
		double lux;
	} lux;
};

struct IPAFrameContext : public FrameContext {
	struct {
		uint32_t exposure;
		double gain;
	} sensor;

	struct Agc : ipa::agc::FrameContext {
		controls::AeMeteringModeEnum meteringMode;
		bool updateMetering;
	} agc;

	ipa::awb::FrameContext awb;

	ipa::ccm::FrameContext ccm;

	ipa::gamma::FrameContext goc;

	struct {
		double lux;
	} lux;
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
