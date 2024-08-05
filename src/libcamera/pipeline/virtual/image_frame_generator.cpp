/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * Copyright (C) 2023, Google Inc.
 *
 * image_frame_generator.cpp - Derived class of FrameGenerator for
 * generating frames from images
 */

#include "image_frame_generator.h"

#include <memory>
#include <optional>
#include <string>

#include <libcamera/base/file.h>
#include <libcamera/base/log.h>

#include <libcamera/framebuffer.h>

#include "libcamera/internal/mapped_framebuffer.h"

#include "libyuv/convert.h"
#include "libyuv/scale.h"
namespace libcamera {

LOG_DECLARE_CATEGORY(Virtual)

std::unique_ptr<ImageFrameGenerator> ImageFrameGenerator::create(
	ImageFrames &imageFrames)
{
	std::unique_ptr<ImageFrameGenerator> imageFrameGenerator =
		std::make_unique<ImageFrameGenerator>();
	imageFrameGenerator->imageFrames_ = &imageFrames;

	/** For each file in the directory
	 *  load the image, convert it to NV12, and store the pointer
	 */
	for (unsigned int i = 0; i < imageFrames.number.value_or(1); i++) {
		std::string path;
		if (!imageFrames.number.has_value()) {
			/* If the path is to an image */
			path = imageFrames.path;
		} else {
			/* If the path is to a directory */
			path = constructPath(imageFrames.path, i);
		}

		File file(path);
		bool isOpen = file.open(File::OpenModeFlag::ReadOnly);
		if (!isOpen) {
			LOG(Virtual, Error) << "Failed to open image file: " << file.fileName();
			return nullptr;
		}

		/* Read the image file to data */
		uint8_t buffer[file.size()];
		Span<unsigned char> data{ buffer, (unsigned long)file.size() };
		long dataSize = file.read(data);

		/* Get the width and height of the image */
		int width, height;
		if (libyuv::MJPGSize(data.data(), dataSize, &width, &height)) {
			LOG(Virtual, Error) << "Failed to get the size of the image file: "
					    << file.fileName();
			return nullptr;
		}

		/* Convert to NV12 and write the data to tmpY and tmpUV */
		int halfWidth = (width + 1) / 2;
		int halfHeight = (height + 1) / 2;
		std::unique_ptr<uint8_t[]> dstY =
			std::make_unique<uint8_t[]>(width * height);
		std::unique_ptr<uint8_t[]> dstUV =
			std::make_unique<uint8_t[]>(halfWidth * halfHeight * 2);
		int ret = libyuv::MJPGToNV12(data.data(), dataSize,
					     dstY.get(), width, dstUV.get(),
					     width, width, height, width, height);
		if (ret != 0) {
			LOG(Virtual, Error) << "MJPGToNV12() failed with " << ret;
		}

		imageFrameGenerator->imageFrameDatas_.emplace_back(
			ImageFrameData{ std::move(dstY), std::move(dstUV),
					Size(width, height) });
	}
	return imageFrameGenerator;
}

std::string ImageFrameGenerator::constructPath(std::string &name, unsigned int &i)
{
	return name + std::to_string(i) + ".jpg";
}

void ImageFrameGenerator::configure(const Size &size)
{
	for (unsigned int i = 0; i < imageFrames_->number.value_or(1); i++) {
		/* Scale the imageFrameDatas_ to scaledY and scaledUV */
		int halfSizeWidth = (size.width + 1) / 2;
		int halfSizeHeight = (size.height + 1) / 2;
		std::unique_ptr<uint8_t[]> scaledY =
			std::make_unique<uint8_t[]>(size.width * size.height);
		std::unique_ptr<uint8_t[]> scaledUV =
			std::make_unique<uint8_t[]>(halfSizeWidth * halfSizeHeight * 2);
		auto &src = imageFrameDatas_[i];

		/*
		 * \todo Implement "contain" & "cover", based on
		 * |imageFrames_[i].scaleMode|.
		 */

		/*
		 * \todo Some platforms might enforce stride due to GPU, like
                 * ChromeOS ciri (64). The weight needs to be a multiple of
                 * the stride to work properly for now.
		 */
		libyuv::NV12Scale(src.Y.get(), src.size.width,
				  src.UV.get(), src.size.width,
				  src.size.width, src.size.height,
				  scaledY.get(), size.width, scaledUV.get(), size.width,
				  size.width, size.height, libyuv::FilterMode::kFilterBilinear);

		/* Store the pointers to member variable */
		scaledFrameDatas_.emplace_back(
			ImageFrameData{ std::move(scaledY), std::move(scaledUV), size });
	}
}

void ImageFrameGenerator::generateFrame(unsigned int &frameCount, const Size &size, const FrameBuffer *buffer)
{
	/* Don't do anything when the list of buffers is empty*/
	if (scaledFrameDatas_.size() == 0)
		return;

	MappedFrameBuffer mappedFrameBuffer(buffer, MappedFrameBuffer::MapFlag::Write);

	auto planes = mappedFrameBuffer.planes();

	/* Make sure the frameCount does not over the number of images */
	frameCount %= imageFrames_->number.value_or(1);

	/* Write the scaledY and scaledUV to the mapped frame buffer */
	libyuv::NV12Copy(scaledFrameDatas_[frameCount].Y.get(), size.width,
			 scaledFrameDatas_[frameCount].UV.get(), size.width, planes[0].begin(),
			 size.width, planes[1].begin(), size.width,
			 size.width, size.height);

	/* proceed an image every 4 frames */
	/* \todo read the parameter_ from the configuration file? */
	parameter_++;
	if (parameter_ % 4 == 0)
		frameCount++;
}

} // namespace libcamera
