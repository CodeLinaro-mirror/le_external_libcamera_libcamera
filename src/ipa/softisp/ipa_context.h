/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * Copyright (C) 2024-2026 Red Hat, Inc.
 *
 * Software ISP IPA Context
 */

#pragma once

#include <array>
#include <optional>
#include <stdint.h>

#include <libcamera/controls.h>

#include "libcamera/internal/matrix.h"
#include "libcamera/internal/vector.h"

#include <libipa/awb.h>
#include <libipa/ccm.h>
#include <libipa/fc_queue.h>

#include "core_ipa_interface.h"

namespace libcamera {

namespace ipa::softisp {

struct IPASessionConfiguration {
	struct {
		int32_t exposureMin, exposureMax;
		double againMin, againMax, again10, againMinStep;
		utils::Duration lineDuration;
		/*
		 * Frame duration control through V4L2_CID_VBLANK. When the
		 * sensor doesn't expose the control, vblankSupported is false
		 * and the frame duration stays at whatever the sensor was
		 * configured with.
		 */
		bool vblankSupported;
		int32_t vblankMin, vblankMax, vblankDef;
		/* Lines the sensor keeps between max exposure and frame length */
		int32_t exposureMargin;
		uint32_t frameHeight;
	} agc;
	struct {
		std::optional<uint8_t> level;
	} black;
};

struct IPAActiveState {
	ipa::awb::ActiveState awb;
	ipa::ccm::ActiveState ccm;

	struct {
		int32_t exposure;
		double again;
		int32_t vblank;
		bool valid;
		utils::Duration minFrameDuration;
		utils::Duration maxFrameDuration;
	} agc;

	struct {
		uint8_t level;
		int32_t lastExposure;
		double lastGain;
	} blc;

	Matrix<float, 3, 3> combinedMatrix;

	struct {
		float gamma;
		/* 0..2 range, 1.0 = normal */
		std::optional<float> contrast;
		std::optional<float> saturation;
	} knobs;
};

struct IPAFrameContext : public FrameContext {
	ipa::awb::FrameContext awb;
	ipa::ccm::FrameContext ccm;

	struct {
		int32_t exposure;
		double gain;
		int32_t vblank;
	} sensor;

	struct {
		utils::Duration minFrameDuration;
		utils::Duration maxFrameDuration;
		utils::Duration frameDuration;
	} agc;

	float gamma;
	std::optional<float> contrast;
	std::optional<float> saturation;
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
	bool ccmEnabled = false;
};

} /* namespace ipa::softisp */

} /* namespace libcamera */
