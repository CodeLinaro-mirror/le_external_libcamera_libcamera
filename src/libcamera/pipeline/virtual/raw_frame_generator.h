/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * Copyright (C) 2026, max.bretschneider@leica-geosystems.com
 *
 * Raw Bayer frame generator for the virtual pipeline handler
 */

#pragma once

#include <filesystem>
#include <memory>
#include <stdint.h>
#include <vector>

#include "frame_generator.h"

namespace libcamera {

/* Frame configuration provided by the config file */
struct RawFrames {
	std::vector<std::filesystem::path> files;
	uint32_t cfaPattern;
	unsigned int bitDepth;
};

class RawFrameGenerator : public FrameGenerator
{
public:
	static std::unique_ptr<RawFrameGenerator> create(RawFrames &rawFrames);

private:
	static constexpr unsigned int frameRepeat = 1; /*advance every frame*/

	struct RawFrameData {
		std::unique_ptr<uint8_t[]> data;
		size_t size;
	};

	void configure(const Size &size) override;
	int generateFrame(const Size &size, const FrameBuffer *buffer) override;

	std::vector<RawFrameData> framesDatas_;
	unsigned int frameIndex_;
	unsigned int parameter_;
};

} /* namespace libcamera */
