/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * Copyright (C) 2020, Raspberry Pi Ltd
 *
 * Helper class for dma-buf allocations.
 */

#pragma once

#include <libcamera/base/flags.h>
#include <libcamera/base/unique_fd.h>

namespace libcamera {

class DmaBufAllocator
{
public:
	enum class DmaBufAllocatorFlag {
		CmaHeap = 1 << 0,
		SystemHeap = 1 << 1,
		UDmaBuf = 1 << 2,
	};

	using DmaBufAllocatorFlags = Flags<DmaBufAllocatorFlag>;

	enum class SyncStep {
		Start = 0,
		End
	};

	enum class SyncType {
		Read = 0,
		Write,
		ReadWrite,
	};

	static void sync(int fd, SyncStep step, SyncType type);

	DmaBufAllocator(DmaBufAllocatorFlags flags = DmaBufAllocatorFlag::CmaHeap);
	~DmaBufAllocator();
	bool isValid() const { return providerHandle_.isValid(); }
	UniqueFD alloc(const char *name, std::size_t size);

private:
	UniqueFD allocFromHeap(const char *name, std::size_t size);
	UniqueFD allocFromUDmaBuf(const char *name, std::size_t size);
	UniqueFD providerHandle_;
	DmaBufAllocatorFlag type_;
};

class DmaSyncer final
{
public:
	explicit DmaSyncer(int fd,
			   DmaBufAllocator::SyncType type = DmaBufAllocator::SyncType::ReadWrite)
		: fd_(fd), type_(type)
	{
		DmaBufAllocator::sync(fd_, DmaBufAllocator::SyncStep::Start, type_);
	}

	~DmaSyncer()
	{
		DmaBufAllocator::sync(fd_, DmaBufAllocator::SyncStep::End, type_);
	}

private:
	int fd_;
	DmaBufAllocator::SyncType type_;
};

LIBCAMERA_FLAGS_ENABLE_OPERATORS(DmaBufAllocator::DmaBufAllocatorFlag)

} /* namespace libcamera */
