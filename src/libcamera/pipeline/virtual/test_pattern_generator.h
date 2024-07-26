/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * Copyright (C) 2023, Google Inc.
 *
 * test_pattern_generator.h - Derived class of FrameGenerator for
 * generating test patterns
 */

#pragma once

#include <memory>

#include <libcamera/framebuffer.h>
#include <libcamera/geometry.h>

#include "frame_generator.h"

namespace libcamera {

enum class TestPattern : char {
	ColorBars = 0,
	DiagonalLines = 1,
};

class TestPatternGenerator : public FrameGenerator
{
private:
	void generateFrame(const Size &size,
			   const FrameBuffer *buffer) override;

protected:
	/* Shift the buffer by 1 pixel left each frame */
	void shiftLeft(const Size &size);
	/* Buffer of test pattern template */
	std::unique_ptr<uint8_t[]> template_;
};

class ColorBarsGenerator : public TestPatternGenerator
{
public:
	static std::unique_ptr<ColorBarsGenerator> create();

private:
	/* Generate a template buffer of the color bar test pattern. */
	void configure(const Size &size) override;
};

class DiagonalLinesGenerator : public TestPatternGenerator
{
public:
	static std::unique_ptr<DiagonalLinesGenerator> create();

private:
	/* Generate a template buffer of the diagonal lines test pattern. */
	void configure(const Size &size) override;
};

} /* namespace libcamera */
