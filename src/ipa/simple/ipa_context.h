/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * Copyright (C) 2024 Red Hat, Inc.
 *
 * Simple pipeline IPA Context
 */

#pragma once

#include <array>
#include <optional>
#include <stdint.h>

#include <libipa/fc_queue.h>
#include <libipa/matrix.h>

namespace libcamera {

namespace ipa::soft {

struct IPASessionConfiguration {
	float gamma;
	struct {
		int32_t exposureMin, exposureMax;
		double againMin, againMax, againMinStep;
	} agc;
	struct {
		std::optional<uint8_t> level;
	} black;
};

struct IPAActiveState {
	struct {
		uint8_t level;
	} blc;

	struct {
		struct {
			double red;
			double green;
			double blue;
		} gains;
		unsigned int temperatureK;
	} awb;

	struct {
		int32_t exposure;
		double again;
	} agc;

	static constexpr unsigned int kGammaLookupSize = 1024;
	struct {
		std::array<double, kGammaLookupSize> gammaTable;
		uint8_t blackLevel;
	} gamma;

	struct {
		Matrix<float, 3, 3> ccm;
		bool enabled;
		bool changed;
	} ccm;
};

struct IPAFrameContext : public FrameContext {
	struct {
		Matrix<float, 3, 3> ccm;
	} ccm;

	struct {
		uint32_t exposure;
		double gain;
	} sensor;
};

struct IPAContext {
	IPASessionConfiguration configuration;
	IPAActiveState activeState;
	FCQueue<IPAFrameContext> frameContexts;
};

} /* namespace ipa::soft */

} /* namespace libcamera */
