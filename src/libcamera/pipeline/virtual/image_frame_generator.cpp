/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * Copyright (C) 2024, Google Inc.
 *
 * image_frame_generator.cpp - Derived class of FrameGenerator for
 * generating frames from images
 */

#include "image_frame_generator.h"

#include <filesystem>
#include <memory>
#include <string>

#include <libcamera/base/file.h>
#include <libcamera/base/log.h>

#include <libcamera/framebuffer.h>

#include "libcamera/internal/mapped_framebuffer.h"

#include "libyuv/convert.h"
#include "libyuv/scale.h"

namespace libcamera {

LOG_DECLARE_CATEGORY(Virtual)

/*
 * Factory function to create an ImageFrameGenerator object.
 * Read the images and convert them to buffers in NV12 format.
 * Store the pointers to the buffers to a list (imageFrameDatas)
 */
std::unique_ptr<ImageFrameGenerator>
ImageFrameGenerator::create(ImageFrames &imageFrames)
{
	std::unique_ptr<ImageFrameGenerator> imageFrameGenerator =
		std::make_unique<ImageFrameGenerator>();
	imageFrameGenerator->imageFrames_ = &imageFrames;

	/*
	 * For each file in the directory, load the image,
	 * convert it to NV12, and store the pointer.
	 */
	for (unsigned int i = 0; i < imageFrames.number.value_or(1); i++) {
		std::filesystem::path path;
		if (!imageFrames.number)
			/* If the path is to an image */
			path = imageFrames.path;
		else
			/* If the path is to a directory */
			path = imageFrames.path / (std::to_string(i) + ".jpg");

		File file(path);
		if (!file.open(File::OpenModeFlag::ReadOnly)) {
			LOG(Virtual, Error) << "Failed to open image file " << file.fileName()
					    << ": " << strerror(file.error());
			return nullptr;
		}

		/* Read the image file to data */
		auto fileSize = file.size();
		auto buffer = std::make_unique<uint8_t[]>(file.size());
		if (file.read({ buffer.get(), static_cast<size_t>(fileSize) }) != fileSize) {
			LOG(Virtual, Error) << "Failed to read file " << file.fileName()
					    << ": " << strerror(file.error());
			return nullptr;
		}

		/* Get the width and height of the image */
		int width, height;
		if (libyuv::MJPGSize(buffer.get(), fileSize, &width, &height)) {
			LOG(Virtual, Error) << "Failed to get the size of the image file: "
					    << file.fileName();
			return nullptr;
		}

		std::unique_ptr<uint8_t[]> dstY =
			std::make_unique<uint8_t[]>(width * height);
		std::unique_ptr<uint8_t[]> dstUV =
			std::make_unique<uint8_t[]>(width * height / 2);
		int ret = libyuv::MJPGToNV12(buffer.get(), fileSize,
					     dstY.get(), width, dstUV.get(),
					     width, width, height, width, height);
		if (ret != 0)
			LOG(Virtual, Error) << "MJPGToNV12() failed with " << ret;

		imageFrameGenerator->imageFrameDatas_.emplace_back(
			ImageFrameData{ std::move(dstY), std::move(dstUV),
					Size(width, height) });
	}

	return imageFrameGenerator;
}

/* Scale the buffers for image frames. */
void ImageFrameGenerator::configure(const Size &size)
{
	/* Reset the source images to prevent multiple configuration calls */
	scaledFrameDatas_.clear();
	frameCount_ = 0;
	parameter_ = 0;

	for (unsigned int i = 0; i < imageFrames_->number.value_or(1); i++) {
		/* Scale the imageFrameDatas_ to scaledY and scaledUV */
		unsigned int halfSizeWidth = (size.width + 1) / 2;
		unsigned int halfSizeHeight = (size.height + 1) / 2;
		std::unique_ptr<uint8_t[]> scaledY =
			std::make_unique<uint8_t[]>(size.width * size.height);
		std::unique_ptr<uint8_t[]> scaledUV =
			std::make_unique<uint8_t[]>(halfSizeWidth * halfSizeHeight * 2);
		auto &src = imageFrameDatas_[i];

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

		scaledFrameDatas_.emplace_back(
			ImageFrameData{ std::move(scaledY), std::move(scaledUV), size });
	}
}

void ImageFrameGenerator::generateFrame(const Size &size, const FrameBuffer *buffer)
{
	/* Don't do anything when the list of buffers is empty*/
	ASSERT(!scaledFrameDatas_.empty());

	MappedFrameBuffer mappedFrameBuffer(buffer, MappedFrameBuffer::MapFlag::Write);

	auto planes = mappedFrameBuffer.planes();

	/* Make sure the frameCount does not over the number of images */
	frameCount_ %= imageFrames_->number.value_or(1);

	/* Write the scaledY and scaledUV to the mapped frame buffer */
	libyuv::NV12Copy(scaledFrameDatas_[frameCount_].Y.get(), size.width,
			 scaledFrameDatas_[frameCount_].UV.get(), size.width, planes[0].begin(),
			 size.width, planes[1].begin(), size.width,
			 size.width, size.height);

	/* Proceed an image every 4 frames */
	/* \todo Consider setting the proceed interval in the config file  */
	parameter_++;
	if (parameter_ % frameInterval == 0)
		frameCount_++;
}

/*
 * \var ImageFrameGenerator::imageFrameDatas_
 * \brief List of pointers to the not scaled image buffers
 */

/*
 * \var ImageFrameGenerator::scaledFrameDatas_
 * \brief List of pointers to the scaled image buffers
 */

/*
 * \var ImageFrameGenerator::imageFrames_
 * \brief Pointer to the imageFrames_ in VirtualCameraData
 */

/*
 * \var ImageFrameGenerator::parameter_
 * \brief Speed parameter. Change to the next image every parameter_ frames
 */

} /* namespace libcamera */
