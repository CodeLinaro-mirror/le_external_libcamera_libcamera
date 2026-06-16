/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * Copyright (C) 2021, Google Inc.
 *
 * IPU3 IPA Context
 *
 */

#pragma once

#include <linux/intel-ipu3.h>

#include <libcamera/base/utils.h>

#include <libcamera/controls.h>
#include <libcamera/geometry.h>

#include <libcamera/ipa/core_ipa_interface.h>

#include <libipa/awb.h>
#include <libipa/ccm.h>
#include <libipa/fc_queue.h>
#include <libipa/gamma.h>
#include <libipa/lsc.h>

namespace libcamera {

namespace ipa::ipu3 {

struct IPASessionConfiguration {
	struct {
		ipu3_uapi_grid_config bdsGrid;
		Size bdsOutputSize;
		uint32_t stride;
	} grid;

	struct {
		ipu3_uapi_grid_config afGrid;
	} af;

	struct {
		utils::Duration minExposureTime;
		utils::Duration maxExposureTime;
		double minAnalogueGain;
		double maxAnalogueGain;
	} agc;

	struct {
		int32_t defVBlank;
		utils::Duration lineDuration;
		Size size;
	} sensor;

	ipa::awb::Session awb;
};

struct IPAActiveState {
	struct {
		uint32_t focus;
		double maxVariance;
		bool stable;
	} af;

	struct {
		uint32_t exposure;
		double gain;
		uint32_t constraintMode;
		uint32_t exposureMode;
	} agc;

	ipa::awb::ActiveState awb;
	ipa::ccm::ActiveState ccm;
	ipa::gamma::ActiveState gamma;
	ipa::lsc::ActiveState lsc;
};

struct IPAFrameContext : public FrameContext {
	struct {
		uint32_t exposure;
		double gain;
	} sensor;

	ipa::awb::FrameContext awb;
	ipa::ccm::FrameContext ccm;
	ipa::gamma::FrameContext gamma;
	ipa::lsc::FrameContext lsc;
};

struct IPAContext {
	IPAContext(unsigned int frameContextSize)
		: frameContexts(frameContextSize)
	{
	}

	IPASessionConfiguration configuration;
	IPAActiveState activeState;

	FCQueue<IPAFrameContext> frameContexts;

	ControlInfoMap::Map ctrlMap;
	IPACameraSensorInfo sensorInfo;
};

} /* namespace ipa::ipu3 */

} /* namespace libcamera*/
