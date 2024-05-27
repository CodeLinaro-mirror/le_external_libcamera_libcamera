/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * Copyright (C) 2020, Raspberry Pi Ltd
 *
 * Helper class for dma-heap allocations.
 */

#include "libcamera/internal/dma_heaps.h"

#include <array>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <linux/dma-buf.h>
#include <linux/dma-heap.h>
#include <linux/udmabuf.h>

#include <libcamera/base/log.h>

/**
 * \file dma_heaps.cpp
 * \brief dma-heap allocator
 */

namespace libcamera {

/*
 * /dev/dma_heap/linux,cma is the dma-heap allocator, which allows dmaheap-cma
 * to only have to worry about importing.
 *
 * Annoyingly, should the cma heap size be specified on the kernel command line
 * instead of DT, the heap gets named "reserved" instead.
 */

#ifndef __DOXYGEN__
struct DmaHeapInfo {
	DmaHeap::DmaHeapFlag type;
	const char *deviceNodeName;
	bool useUDmaBuf;
};
#endif

static constexpr std::array<DmaHeapInfo, 4> heapInfos = { {
	{ DmaHeap::DmaHeapFlag::Cma, "/dev/dma_heap/linux,cma", false },
	{ DmaHeap::DmaHeapFlag::Cma, "/dev/dma_heap/reserved", false },
	{ DmaHeap::DmaHeapFlag::System, "/dev/dma_heap/system", false },
	{ DmaHeap::DmaHeapFlag::System, "/dev/udmabuf", true },
} };

LOG_DEFINE_CATEGORY(DmaHeap)

/**
 * \class DmaHeap
 * \brief Helper class for dma-heap allocations
 *
 * DMA heaps are kernel devices that provide an API to allocate memory from
 * different pools called "heaps", wrap each allocated piece of memory in a
 * dmabuf object, and return the dmabuf file descriptor to userspace. Multiple
 * heaps can be provided by the system, with different properties for the
 * underlying memory.
 *
 * This class wraps a DMA heap selected at construction time, and exposes
 * functions to manage memory allocation.
 */

/**
 * \enum DmaHeap::DmaHeapFlag
 * \brief Type of the dma-heap
 * \var DmaHeap::Cma
 * \brief Allocate from a CMA dma-heap, providing physically-contiguous memory
 * \var DmaHeap::System
 * \brief Allocate from the system dma-heap, using the page allocator
 */

/**
 * \typedef DmaHeap::DmaHeapFlags
 * \brief A bitwise combination of DmaHeap::DmaHeapFlag values
 */

/**
 * \brief Construct a DmaHeap of a given type
 * \param[in] type The type(s) of the dma-heap(s) to allocate from
 *
 * The DMA heap type is selected with the \a type parameter, which defaults to
 * the CMA heap. If no heap of the given type can be accessed, the constructed
 * DmaHeap instance is invalid as indicated by the isValid() function.
 *
 * Multiple types can be selected by combining type flags, in which case the
 * constructed DmaHeap will match one of the types. If the system provides
 * multiple heaps that match the requested types, which heap is used is
 * undefined.
 */
DmaHeap::DmaHeap(DmaHeapFlags type)
{
	for (const auto &info : heapInfos) {
		if (!(type & info.type))
			continue;

		int ret = ::open(info.deviceNodeName, O_RDWR | O_CLOEXEC, 0);
		if (ret < 0) {
			ret = errno;
			LOG(DmaHeap, Debug)
				<< "Failed to open " << info.deviceNodeName << ": "
				<< strerror(ret);
			continue;
		}

		LOG(DmaHeap, Debug) << "Using " << info.deviceNodeName;
		dmaHeapHandle_ = UniqueFD(ret);
		useUDmaBuf_ = info.useUDmaBuf;
		break;
	}

	if (!dmaHeapHandle_.isValid())
		LOG(DmaHeap, Error) << "Could not open any dmaHeap device";
}

/**
 * \brief Destroy the DmaHeap instance
 */
DmaHeap::~DmaHeap() = default;

/**
 * \fn DmaHeap::isValid()
 * \brief Check if the DmaHeap instance is valid
 * \return True if the DmaHeap is valid, false otherwise
 */

UniqueFD DmaHeap::allocFromHeap(const char *name, std::size_t size)
{
	struct dma_heap_allocation_data alloc = {};
	int ret;

	alloc.len = size;
	alloc.fd_flags = O_CLOEXEC | O_RDWR;

	ret = ::ioctl(dmaHeapHandle_.get(), DMA_HEAP_IOCTL_ALLOC, &alloc);
	if (ret < 0) {
		LOG(DmaHeap, Error) << "dmaHeap allocation failure for " << name;
		return {};
	}

	UniqueFD allocFd(alloc.fd);
	ret = ::ioctl(allocFd.get(), DMA_BUF_SET_NAME, name);
	if (ret < 0) {
		LOG(DmaHeap, Error) << "dmaHeap naming failure for " << name;
		return {};
	}

	return allocFd;
}

UniqueFD DmaHeap::allocFromUDmaBuf(const char *name, std::size_t size)
{
	/* size must be a multiple of the page-size round it up */
	std::size_t pageMask = sysconf(_SC_PAGESIZE) - 1;
	size = (size + pageMask) & ~pageMask;

	int ret = memfd_create(name, MFD_ALLOW_SEALING);
	if (ret < 0) {
		ret = errno;
		LOG(DmaHeap, Error)
			<< "UdmaHeap failed to allocate memfd storage: "
			<< strerror(ret);
		return {};
	}

	UniqueFD memfd(ret);

	ret = ftruncate(memfd.get(), size);
	if (ret < 0) {
		ret = errno;
		LOG(DmaHeap, Error)
			<< "UdmaHeap failed to set memfd size: " << strerror(ret);
		return {};
	}

	/* UdmaHeap Buffers *must* have the F_SEAL_SHRINK seal */
	ret = fcntl(memfd.get(), F_ADD_SEALS, F_SEAL_SHRINK);
	if (ret < 0) {
		ret = errno;
		LOG(DmaHeap, Error)
			<< "UdmaHeap failed to seal the memfd: " << strerror(ret);
		return {};
	}

	struct udmabuf_create create;

	create.memfd = memfd.get();
	create.flags = UDMABUF_FLAGS_CLOEXEC;
	create.offset = 0;
	create.size = size;

	ret = ::ioctl(dmaHeapHandle_.get(), UDMABUF_CREATE, &create);
	if (ret < 0) {
		ret = errno;
		LOG(DmaHeap, Error)
			<< "UdmaHeap failed to allocate " << size << " bytes: "
			<< strerror(ret);
		return {};
	}

	if (create.size < size) {
		LOG(DmaHeap, Error)
			<< "UdmaHeap allocated " << create.size << " bytes instead of "
			<< size << " bytes";
		return {};
	}

	if (create.size != size)
		LOG(DmaHeap, Warning)
			<< "UdmaHeap allocated " << create.size << " bytes, "
			<< "which is greater than requested : " << size << " bytes";

	/* Fail if not suitable, the allocation will be free'd by UniqueFD */
	LOG(DmaHeap, Debug) << "UdmaHeap allocated " << create.size << " bytes";

	/* The underlying memfd is kept as as a reference in the kernel */
	UniqueFD uDma(ret);

	return uDma;
}

/**
 * \brief Allocate a dma-buf from the DmaHeap
 * \param [in] name The name to set for the allocated buffer
 * \param [in] size The size of the buffer to allocate
 *
 * Allocates a dma-buf with read/write access.
 *
 * If the allocation fails, return an invalid UniqueFD.
 *
 * \return The UniqueFD of the allocated buffer
 */
UniqueFD DmaHeap::alloc(const char *name, std::size_t size)
{
	if (!name)
		return {};

	if (useUDmaBuf_)
		return allocFromUDmaBuf(name, size);
	else
		return allocFromHeap(name, size);
}

} /* namespace libcamera */
