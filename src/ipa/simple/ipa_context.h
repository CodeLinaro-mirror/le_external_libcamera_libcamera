/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * Copyright (C) 2024 Red Hat, Inc.
 *
 * Simple pipeline IPA Context
 *
 */

#pragma once

#include <array>
#include <stdint.h>

#include <libipa/fc_queue.h>

namespace libcamera {

namespace ipa::soft {

struct IPASessionConfiguration {
	float gamma;
	struct {
		uint8_t level;
		bool set;
		bool changed;
	} black;
};

struct IPAActiveState {
	struct {
		unsigned int red;
		unsigned int green;
		unsigned int blue;
	} gains;
	static constexpr unsigned int kGammaLookupSize = 1024;
	std::array<double, kGammaLookupSize> gammaTable;
};

struct IPAFrameContext : public FrameContext {
};

struct IPAContext {
	IPASessionConfiguration configuration;
	IPAActiveState activeState;
	FCQueue<IPAFrameContext> frameContexts;
};

} /* namespace ipa::soft */

} /* namespace libcamera */
