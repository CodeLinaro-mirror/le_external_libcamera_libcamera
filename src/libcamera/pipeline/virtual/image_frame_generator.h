/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * Copyright (C) 2023, Google Inc.
 *
 * image_frame_generator.h - Derived class of FrameGenerator for
 * generating frames from images
 */

#pragma once

#include <memory>
#include <optional>
#include <stdint.h>
#include <sys/types.h>

#include "frame_generator.h"

namespace libcamera {

enum class ScaleMode : char {
	Fill = 0,
	Contain = 1,
	Cover = 2,
};

/* Frame configuration provided by the config file */
struct ImageFrames {
	std::string path;
	ScaleMode scaleMode;
	std::optional<unsigned int> number;
};

class ImageFrameGenerator : public FrameGenerator
{
public:
	/** Factory function to create an ImageFrameGenerator object.
	 *  Read the images and convert them to buffers in NV12 format.
	 *  Store the pointers to the buffers to a list (imageFrameDatas)
	 */
	static std::unique_ptr<ImageFrameGenerator> create(ImageFrames &imageFrames);

private:
	struct ImageFrameData {
		std::unique_ptr<uint8_t[]> Y;
		std::unique_ptr<uint8_t[]> UV;
		Size size;
	};

	/* Scale the buffers for image frames. */
	void configure(const Size &size) override;
	void generateFrame(unsigned int &frameCount, const Size &size, const FrameBuffer *buffer) override;

	static std::string constructPath(std::string &name, unsigned int &i);

	/* List of pointers to the not scaled image buffers */
	std::vector<ImageFrameData> imageFrameDatas_;
	/* List of pointers to the scaled image buffers */
	std::vector<ImageFrameData> scaledFrameDatas_;
	/* Pointer to the imageFrames_ in VirtualCameraData */
	ImageFrames *imageFrames_;
	/* Speed parameter. Change to the next image every parameter_ frames. */
	int parameter_;
};

} /* namespace libcamera */
