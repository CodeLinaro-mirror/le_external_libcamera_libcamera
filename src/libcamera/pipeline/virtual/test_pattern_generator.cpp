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

	/* TODO: select whether to do shifting or not */
	shiftLeft(size);

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

void TestPatternGenerator::shiftLeft(const Size &size)
{
	/* Store the first column temporarily */
	uint8_t firstColumn[size.height * kARGBSize];
	for (size_t h = 0; h < size.height; h++) {
		unsigned int index = h * size.width * kARGBSize;
		unsigned int index1 = h * kARGBSize;
		firstColumn[index1] = template_[index];
		firstColumn[index1 + 1] = template_[index + 1];
		firstColumn[index1 + 2] = template_[index + 2];
		firstColumn[index1 + 3] = 0x00;
	}

	/* Overwrite template_ */
	uint8_t *buf = template_.get();
	for (size_t h = 0; h < size.height; h++) {
		for (size_t w = 0; w < size.width - 1; w++) {
			/* Overwrite with the pixel on the right */
			unsigned int index = (h * size.width + w + 1) * kARGBSize;
			*buf++ = template_[index]; // B
			*buf++ = template_[index + 1]; // G
			*buf++ = template_[index + 2]; // R
			*buf++ = 0x00; // A
		}
		/* Overwrite the new last column with the original first column */
		unsigned int index1 = h * kARGBSize;
		*buf++ = firstColumn[index1]; // B
		*buf++ = firstColumn[index1 + 1]; // G
		*buf++ = firstColumn[index1 + 2]; // R
		*buf++ = 0x00; // A
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
		{ 0xff, 0xff, 0xff }, // White
		{ 0xff, 0xff, 0x00 }, // Yellow
		{ 0x00, 0xff, 0xff }, // Cyan
		{ 0x00, 0xff, 0x00 }, // Green
		{ 0xff, 0x00, 0xff }, // Magenta
		{ 0xff, 0x00, 0x00 }, // Red
		{ 0x00, 0x00, 0xff }, // Blue
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
		{ 0xff, 0xff, 0xff }, // White
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
