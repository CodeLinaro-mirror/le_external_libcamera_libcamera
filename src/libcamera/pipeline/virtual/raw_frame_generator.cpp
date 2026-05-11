/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * Copyright (C) 2026, max.bretschneider@leica-geosystems.com
 *
 * Derived class of FrameGenerator for generating raw Bayer frames
 */

#include "raw_frame_generator.h"

#include <errno.h>
#include <string.h>

#include <libcamera/base/file.h>
#include <libcamera/base/log.h>

#include <libcamera/framebuffer.h>

#include "libcamera/internal/mapped_framebuffer.h"

namespace libcamera {

LOG_DECLARE_CATEGORY(Virtual)

/*
 * Factory function to create a RawFrameGenerator object.
 * Read the raw Bayer frames from disk and store them in memory.
 */
std::unique_ptr<RawFrameGenerator>
RawFrameGenerator::create(RawFrames &rawFrames)
{
	std::unique_ptr<RawFrameGenerator> rawFrameGenerator =
		std::make_unique<RawFrameGenerator>();

	/*
	 * For each file in the directory, load the raw frame
	 * and store it. No format conversion is performed, but
	 * raw Bayer bytes are stored as-is.
	 */
	for (const auto &path : rawFrames.files) {
		File file(path);
		if (!file.open(File::OpenModeFlag::ReadOnly)) {
			LOG(Virtual, Error) << "Failed to open raw frame file "
					    << file.fileName()
					    << ": " << strerror(file.error());
			return nullptr;
		}

		auto fileSize = file.size();
		auto buffer = std::make_unique<uint8_t[]>(fileSize);
		if (file.read({ buffer.get(), static_cast<size_t>(fileSize) }) != fileSize) {
			LOG(Virtual, Error) << "Failed to read raw frame file "
					    << file.fileName()
					    << ": " << strerror(file.error());
			return nullptr;
		}

		rawFrameGenerator->framesDatas_.emplace_back(
			RawFrameData{ std::move(buffer), static_cast<size_t>(fileSize) });
	}

	ASSERT(!rawFrameGenerator->framesDatas_.empty());

	return rawFrameGenerator;
}

void RawFrameGenerator::configure(const Size & /*size*/)
{
	/*
	 * Raw frames cannot be scaled, the configured size is not used for
	 * processing but mismatches are caught in generateFrame().
	 */
	frameIndex_ = 0;
	parameter_ = 0;
}

int RawFrameGenerator::generateFrame(const Size & /*size*/, const FrameBuffer *buffer)
{
	ASSERT(!framesDatas_.empty());

	MappedFrameBuffer mappedFrameBuffer(buffer,
					    MappedFrameBuffer::MapFlag::Write);

	const auto &planes = mappedFrameBuffer.planes();

	/* Loop only around the number of frames available */
	frameIndex_ %= framesDatas_.size();

	const auto &frame = framesDatas_[frameIndex_];

	/*
	 * Raw Bayer frames must exactly match the configured output size.
	 * They cannot be scaled to fit.
	 */
	if (frame.size != planes[0].size()) {
		LOG(Virtual, Error) << "Raw frame size mismatch: file has "
				    << frame.size << " bytes, buffer expects "
				    << planes[0].size() << " bytes";
		return -EINVAL;
	}

	memcpy(planes[0].data(), frame.data.get(), frame.size);

	/* Proceed to the next frame on every request */
	parameter_++;
	if (parameter_ % frameRepeat == 0) {
		frameIndex_++;
	}

	return 0;
}

/*
 * \var RawFrameGenerator::frameRepeat
 * \brief Number of frames to repeat before proceeding to the next frame
 */

/*
 * \var RawFrameGenerator::framesDatas_
 * \brief List of raw Bayer frame buffers loaded from disk
 */

/* \var RawFrameGenerator::frameIndex_
 * \brief Index of the current frame in framesDatas_
 */

/*
 * \var RawFrameGenerator::parameter_
 * \brief Counter used to implement frameRepeat behaviour
 */

} /* namespace libcamera */
