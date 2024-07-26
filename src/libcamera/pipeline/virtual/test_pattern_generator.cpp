/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * Copyright (C) 2023, Google Inc.
 *
 * test_pattern_generator.cpp - Derived class of FrameGenerator for
 * generating test patterns
 */

#include "test_pattern_generator.h"

#include <libcamera/base/log.h>

#include "libcamera/internal/mapped_framebuffer.h"

#include "libyuv/convert_from_argb.h"
namespace libcamera {

LOG_DECLARE_CATEGORY(Virtual)

static const unsigned int kARGBSize = 4;

void TestPatternGenerator::generateFrame(
	const Size &size,
	const FrameBuffer *buffer)
{
	MappedFrameBuffer mappedFrameBuffer(buffer,
					    MappedFrameBuffer::MapFlag::Write);

	auto planes = mappedFrameBuffer.planes();

	/* Convert the template_ to the frame buffer */
	int ret = libyuv::ARGBToNV12(
		template_.get(), /*src_stride_argb=*/size.width * kARGBSize,
		planes[0].begin(), size.width,
		planes[1].begin(), size.width,
		size.width, size.height);
	if (ret != 0) {
		LOG(Virtual, Error) << "ARGBToNV12() failed with " << ret;
	}
}

std::unique_ptr<ColorBarsGenerator> ColorBarsGenerator::create()
{
	return std::make_unique<ColorBarsGenerator>();
}

void ColorBarsGenerator::configure(const Size &size)
{
	constexpr uint8_t kColorBar[8][3] = {
		//  R,    G,    B
		{ 0xFF, 0xFF, 0xFF }, // White
		{ 0xFF, 0xFF, 0x00 }, // Yellow
		{ 0x00, 0xFF, 0xFF }, // Cyan
		{ 0x00, 0xFF, 0x00 }, // Green
		{ 0xFF, 0x00, 0xFF }, // Magenta
		{ 0xFF, 0x00, 0x00 }, // Red
		{ 0x00, 0x00, 0xFF }, // Blue
		{ 0x00, 0x00, 0x00 }, // Black
	};

	template_ = std::make_unique<uint8_t[]>(
		size.width * size.height * kARGBSize);

	unsigned int colorBarWidth = size.width / std::size(kColorBar);

	uint8_t *buf = template_.get();
	for (size_t h = 0; h < size.height; h++) {
		for (size_t w = 0; w < size.width; w++) {
			// repeat when the width is exceed
			int index = (w / colorBarWidth) % std::size(kColorBar);

			*buf++ = kColorBar[index][2]; // B
			*buf++ = kColorBar[index][1]; // G
			*buf++ = kColorBar[index][0]; // R
			*buf++ = 0x00; // A
		}
	}
}

std::unique_ptr<DiagonalLinesGenerator> DiagonalLinesGenerator::create()
{
	return std::make_unique<DiagonalLinesGenerator>();
}

void DiagonalLinesGenerator::configure(const Size &size)
{
	constexpr uint8_t kColorBar[8][3] = {
		//  R,    G,    B
		{ 0xFF, 0xFF, 0xFF }, // White
		{ 0x00, 0x00, 0x00 }, // Black
	};

	template_ = std::make_unique<uint8_t[]>(
		size.width * size.height * kARGBSize);

	unsigned int lineWidth = size.width / 10;

	uint8_t *buf = template_.get();
	for (size_t h = 0; h < size.height; h++) {
		for (size_t w = 0; w < size.width; w++) {
			// repeat when the width is exceed
			int index = ((w + h) / lineWidth) % 2;

			*buf++ = kColorBar[index][2]; // B
			*buf++ = kColorBar[index][1]; // G
			*buf++ = kColorBar[index][0]; // R
			*buf++ = 0x00; // A
		}
	}
}

} /* namespace libcamera */
