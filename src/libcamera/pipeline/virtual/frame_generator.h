/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * Copyright (C) 2023, Google Inc.
 *
 * frame_generator.h - Virtual cameras helper to generate frames
 */

#pragma once

#include <libcamera/framebuffer.h>
#include <libcamera/geometry.h>

namespace libcamera {

class FrameGenerator
{
public:
	virtual ~FrameGenerator() = default;

	/* Create buffers for using them in `generateFrame` */
	virtual void configure(const Size &size) = 0;

	/** Fill the output frame buffer.
	 * Use the frame at the frameCount of image frames
	 */
	virtual void generateFrame(unsigned int &frameCount, const Size &size,
				   const FrameBuffer *buffer) = 0;

protected:
	FrameGenerator() {}
};

} /* namespace libcamera */
