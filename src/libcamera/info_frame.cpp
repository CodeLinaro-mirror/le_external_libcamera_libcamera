/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * Copyright (C) 2023, Google Inc.
 *
 * info_frame.cpp - InfoFrame and InfoFramePool
 */

#include "libcamera/internal/info_frame.h"

#include <sys/mman.h>
#include <unistd.h>

#include "libcamera/internal/dma_buf_allocator.h"
#include "libcamera/internal/formats.h"
#include "libcamera/internal/pool.h"

namespace libcamera {

LOG_DEFINE_CATEGORY(InfoFrame)

/**
 * \class InfoFrame
 * \brief A wrapper class that consists of a FrameBuffer and extra information,
 * including PixelFormat, size, and memory address if it's mmap'ed.
 */

/**
 * \class InfoFrame::Plane
 * \brief Per-plane frame mmap'ed address
 *
 * Frames are stored in memory in one or multiple planes. The
 * InfoFrame::Plane structure stores per-plane mmap'ed address.
 */

/**
 * \var InfoFrame::Plane::address
 * \brief mmap'ed address of a plane
 */

InfoFrame::InfoFrame() = default;

/**
 * \brief Create an InfoFrame instance
 * \param[in] format PixelFormat of the frame
 * \param[in] size Size of the frame
 * \param[in] buffer Pointer to the buffer. InfoFrame doesn't take the ownership
 * \param[in] strideAlign The stride alignment, in bytes (1 for default alignment)
 * \param[in] scanAlign The scanline alignment, in bytes (1 for default alignment)
 * \param[in] planes mmap'ed addresses of planes
 */
InfoFrame::InfoFrame(const PixelFormat &format, const Size &size,
		     FrameBuffer *buffer, unsigned int strideAlign,
		     unsigned int scanAlign, std::array<Plane, 3> planes)
	: size_(size), format_(format), buffer_(buffer),
	  strideAlign_(strideAlign), scanAlign_(scanAlign),
	  planes_(planes)

{
}

/**
 * \brief Get the mmap'ed address of \a plane
 * \param[in] plane The \a plane'th plane
 * \return Return the mmap'ed address of the plane
 */
uint8_t *InfoFrame::address(unsigned int plane) const
{
	return (plane < numPlanes()) ? planes_[plane].address : nullptr;
}

/**
 * \fn InfoFrame::format()
 * \return Return the PixelFormat of the frame
 */

/**
 * \fn InfoFrame::size()
 * \return Return the size of the frame
 */

/**
 * \fn InfoFrame::buffer()
 * \return Return the pointer to the buffer
 */

/**
 * \fn InfoFrame::numPlanes()
 * \return Return the number of planes of the buffer
 */

/**
 * \fn InfoFrame::strideAlign()
 * \return Return the stride alignment, in bytes (1 for default alignment)
 */

/**
 * \fn InfoFrame::scanAlign()
 * \return Return the scanline alignment, in bytes (1 for default alignment)
 */

/**
 * \class InfoFramePool
 * \brief A buffer pool that allows users to allocate and request InfoFrame
 * buffers from DmaBufAllocator
 */

/**
 * \struct InfoFramePool::MappedBufferInfo
 * \brief Contains the information of the mmap'ed buffers, including address
 * and length
 */

/**
 * \var InfoFramePool::MappedBufferInfo::address
 * \brief mmap'ed address of a buffer
 */

/**
 * \var InfoFramePool::MappedBufferInfo::dmabufLength
 * \brief Length of the DMA buffer
 */

InfoFramePool::InfoFramePool() = default;

InfoFramePool::~InfoFramePool()
{
	release();
}

/**
 * \brief Create DMA buffers
 * \param[in] dmaHeap The DmaBufAllocator to allocate buffers from
 * \param[in] format PixelFormat of each frame
 * \param[in] size Size of each frame
 * \param[in] count Number of frames to create
 * \param[in] strideAlign The stride alignment, in bytes (1 for default alignment)
 * \param[in] scanAlign The scanline alignment, in bytes (1 for default alignment)
 *
 * \return 0 on success or a negative error code otherwise
 */
int InfoFramePool::createBuffers(DmaBufAllocator *dmaHeap,
				 const PixelFormat &format, const Size &size,
				 unsigned int count,
				 unsigned int strideAlign, unsigned scanAlign)
{
	release();

	const PixelFormatInfo &info = PixelFormatInfo::info(format);
	uint32_t frameSize = info.frameSize(size, strideAlign, scanAlign);

	std::vector<std::unique_ptr<FrameBuffer>> buffers;
	buffers.reserve(count);
	for (unsigned int i = 0; i < count; i++) {
		SharedFD fd(dmaHeap->alloc(("frame-" + std::to_string(i)).c_str(),
					   frameSize));
		if (!fd.isValid()) {
			buffers.clear();
			return -EBUSY;
		}

		uint32_t offset = 0;
		std::vector<FrameBuffer::Plane> planes;

		for (unsigned int j = 0; j < info.numPlanes(); j++) {
			planes.emplace_back(FrameBuffer::Plane{
				fd, offset,
				info.planeSize(size, j, strideAlign, scanAlign),
				info.stride(size.width, j, strideAlign) });
			offset += planes.back().length;
		}

		buffers.emplace_back(std::make_unique<FrameBuffer>(planes));
	}

	setBuffers(format, size, buffers, strideAlign, scanAlign);

	return 0;
}

/**
 * \brief Release all allocated buffers. InfoFramePool can create another set
 * of buffers again.
 */
void InfoFramePool::release()
{
	if (munmap())
		LOG(InfoFrame, Error) << "Failed to unmap mapped buffers";

	pool_.release();
}

/**
 * \brief Fetch an available InfoFrame from the pool
 * \param[out] mailBox The shared pointer of MailBox to store the fetched
 * InfoFrame
 *
 * When \a mailBox is reset, the recycler will return the InfoFrame back to the
 * InfoFramePool.
 */
void InfoFramePool::fetch(SharedMailBox<InfoFrame> &mailBox)
{
	auto recycler = [this](InfoFrame &info) {
		this->put(info);
	};

	mailBox->put(get(), recycler);
}

/**
 * \brief mmap all allocated buffers and set the addresses in `InfoFrame`s
 */
int InfoFramePool::mmap()
{
	if (!mappedBuffers_.empty())
		return 0;

	for (auto &buffer : pool_.content()) {
		for (const FrameBuffer::Plane &plane : buffer->planes()) {
			const int fd = plane.fd.get();
			if (mappedBuffers_.find(fd) == mappedBuffers_.end()) {
				const size_t length = lseek(fd, 0, SEEK_END);
				mappedBuffers_[fd] = MappedBufferInfo{ nullptr, length };
			}
		}
	}

	for (auto &[fd, info] : mappedBuffers_) {
		void *address = ::mmap(nullptr, info.dmabufLength,
				       PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
		info.address = static_cast<uint8_t *>(address);
	}
	return 0;
}

/**
 * \brief munmap all allocated buffers and reset addresses
 */
int InfoFramePool::munmap()
{
	if (mappedBuffers_.empty())
		return 0;

	for (auto &[_, info] : mappedBuffers_) {
		::munmap(info.address, info.dmabufLength);
		info.address = nullptr;
	}

	mappedBuffers_.clear();
	return 0;
}

/**
 * \fn InfoFramePool::mapped()
 * \return True if mmap() was called on the allocated buffers, false otherwise
 */

/**
 * \fn InfoFramePool::size()
 * \return Return the number of `InfoFrame`s allocated
 */

/**
 * \fn InfoFramePool::content()
 * \return Return frame buffers allocated in the pool
 */

void InfoFramePool::setBuffers(const PixelFormat &format, const Size &size,
			       std::vector<std::unique_ptr<FrameBuffer>> &buffers,
			       unsigned int strideAlign, unsigned int scanAlign)
{
	if (munmap())
		LOG(InfoFrame, Error) << "Failed to unmap buffers";

	size_ = size;
	format_ = format;
	pool_.setData(buffers);
	strideAlign_ = strideAlign;
	scanAlign_ = scanAlign;
}

InfoFrame InfoFramePool::get()
{
	FrameBuffer *buffer = pool_.get();

	std::array<InfoFrame::Plane, 3> planes;
	for (size_t i = 0; i < buffer->planes().size() && i < 3; i++) {
		const int fd = buffer->planes()[i].fd.get();
		const unsigned int offset = buffer->planes()[i].offset;

		if (mappedBuffers_.count(fd))
			planes[i].address = mappedBuffers_[fd].address + offset;
	}

	return InfoFrame(format_, size_, buffer, strideAlign_, scanAlign_, planes);
}

void InfoFramePool::put(InfoFrame &info)
{
	pool_.put(info.buffer());
}

} /* namespace libcamera */
