/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * Copyright (C) 2024-2026 Red Hat, Inc.
 *
 * Simple pipeline IPA Context
 */

#pragma once

#include <optional>
#include <stdint.h>

#include <libcamera/controls.h>

#include "libcamera/internal/matrix.h"
#include "libcamera/internal/vector.h"

#include <libipa/agc_mean_luminance.h>
#include <libipa/camera_sensor_helper.h>
#include <libipa/fc_queue.h>

#include "core_ipa_interface.h"

#include "agc_simple.h"

namespace libcamera {

namespace ipa::soft {

struct IPASessionConfiguration {
	struct {
		AgcSimpleAlgorithm::Session simple;
		AgcMeanLuminanceAlgorithm::Session ml;
		double again10, againMinStep;
	} agc;
	struct {
		std::optional<uint8_t> level;
	} black;
};

struct IPAActiveState {
	struct {
		AgcSimpleAlgorithm::ActiveState simple;
		AgcMeanLuminanceAlgorithm::ActiveState ml;
	} agc;

	struct {
		uint8_t level;
		int32_t lastExposure;
		double lastGain;
	} blc;

	struct {
		RGB<float> gains;
		unsigned int temperatureK;
	} awb;

	Matrix<float, 3, 3> combinedMatrix;

	struct {
		float gamma;
		/* 0..2 range, 1.0 = normal */
		std::optional<float> contrast;
		std::optional<float> saturation;
	} knobs;
};

struct IPAFrameContext : public FrameContext {
	Matrix<float, 3, 3> ccm;

	struct {
		AgcSimpleAlgorithm::FrameContext simple;
		AgcMeanLuminanceAlgorithm::FrameContext ml;
		int32_t exposure;
		double gain;
	} agc;

	struct {
		int32_t exposure;
		double gain;
	} sensor;

	RGB<float> gains;

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
	ControlInfoMap sensorControls;
	IPASessionConfiguration configuration;
	IPAActiveState activeState;
	FCQueue<IPAFrameContext> frameContexts;
	ControlInfoMap::Map ctrlMap;
	std::unique_ptr<CameraSensorHelper> camHelper;
	bool ccmEnabled = false;
};

} /* namespace ipa::soft */

} /* namespace libcamera */
