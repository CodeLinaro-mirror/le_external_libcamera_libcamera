/*
 * Copyright (C) 2023, Google Inc.
 *
 * info_frame.h - InfoFrame and InfoFramePool
 */

#pragma once

#include <array>
#include <memory>
#include <unordered_map>
#include <vector>

#include <libcamera/framebuffer.h>
#include <libcamera/geometry.h>
#include <libcamera/pixel_format.h>

#include "libcamera/internal/dma_buf_allocator.h"
#include "libcamera/internal/mailbox.h"
#include "libcamera/internal/pool.h"

namespace libcamera {

class InfoFrame
{
public:
	struct Plane {
		uint8_t *address;
	};

	InfoFrame();
	InfoFrame(const PixelFormat &format, const Size &size, FrameBuffer *buffers,
		  unsigned int strideAlign = 1, unsigned int scanAlign = 1,
		  std::array<Plane, 3> planes = {});

	uint8_t *address(unsigned int plane) const;

	PixelFormat format() const { return format_; }
	Size size() const { return size_; }
	FrameBuffer *buffer() const { return buffer_; }
	unsigned int numPlanes() const { return buffer_ ? buffer_->planes().size() : 0; }
	unsigned int strideAlign() const { return strideAlign_; }
	unsigned int scanAlign() const { return scanAlign_; }

private:
	Size size_;
	PixelFormat format_;
	FrameBuffer *buffer_ = nullptr;

	unsigned int strideAlign_ = 1;
	unsigned int scanAlign_ = 1;

	std::array<Plane, 3> planes_;
};

class InfoFramePool
{
public:
	struct MappedBufferInfo {
		uint8_t *address = nullptr;
		size_t dmabufLength = 0;
	};

	InfoFramePool();
	~InfoFramePool();

	int createBuffers(DmaBufAllocator *dmaHeap, const PixelFormat &format,
			  const Size &size, uint32_t count,
			  unsigned int strideAlign = 1, unsigned scanAlign = 1);

	void release();

	void fetch(SharedMailBox<InfoFrame> &mailBox);

	int mmap();
	int munmap();

	bool mapped() const { return 0 != mappedBuffers_.size(); }

	size_t size() { return pool_.size(); }
	std::vector<std::unique_ptr<FrameBuffer>> &content()
	{
		return pool_.content();
	}

private:
	LIBCAMERA_DISABLE_COPY_AND_MOVE(InfoFramePool)

	void setBuffers(const PixelFormat &format, const Size &size,
			std::vector<std::unique_ptr<FrameBuffer>> &buffers,
			unsigned int align, unsigned int scanAlign);

	InfoFrame get();
	void put(InfoFrame &frameInfo);

	Size size_;
	PixelFormat format_;
	Pool<FrameBuffer *, std::unique_ptr<FrameBuffer>> pool_;
	unsigned int strideAlign_;
	unsigned int scanAlign_;

	std::unordered_map<int, MappedBufferInfo> mappedBuffers_;
};

} /* namespace libcamera */
