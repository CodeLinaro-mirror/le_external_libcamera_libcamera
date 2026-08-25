/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * Copyright (C) 2022, Google Inc.
 *
 * IPA Frame context queue
 */

#pragma once

#include <algorithm>
#include <memory>
#include <optional>
#include <stdint.h>
#include <type_traits>

#include <libcamera/base/log.h>

namespace libcamera {

LOG_DECLARE_CATEGORY(FCQueue)

namespace ipa {

template<typename T>
class FCQueue
{
	static_assert(std::is_default_constructible_v<T>);

public:
	FCQueue(std::size_t capacity)
		: entries_(std::make_unique<std::optional<Entry>[]>(capacity)),
		  capacity_(capacity)
	{
		ASSERT(capacity > 0);
	}

	void clear()
	{
		next_ = 0;
		lastFrame_.reset();

		for (size_t i = 0; i < capacity_; i++)
			entries_[i].reset();
	}

	T &get(uint32_t frame)
	{
		LOG(FCQueue, Debug) << "get(" << frame << ")";

		if (auto *d = find(frame))
			return *d;

		LOG(FCQueue, Warning)
			<< "Frame " << frame << " not found, trying to allocate";

		return allocNext(frame);
	}

	T &alloc(uint32_t frame)
	{
		LOG(FCQueue, Debug) << "alloc(" << frame << ")";

		if (frame <= lastFrame_) {
			if (auto *d = find(frame)) {
				LOG(FCQueue, Warning)
					<< "Frame " << frame << " already initialised";
				return *d;
			}
		}

		return allocNext(frame);
	}

private:
	LIBCAMERA_DISABLE_COPY_AND_MOVE(FCQueue)

	struct Entry {
		uint32_t frame;
		T data;

		Entry(uint32_t f)
			: frame(f), data()
		{
		}
	};

	T *find(uint32_t frame)
	{
		const auto findInRange = [&](auto first, auto last) -> T * {
			auto it = std::partition_point(first, last, [&](const auto &e) {
				ASSERT(e);
				return e->frame < frame;
			});
			if (it != last && (*it)->frame == frame)
				return &(*it)->data;

			return nullptr;
		};

		const auto first = entries_.get();
		const auto mid = first + next_;
		const auto last = first + capacity_;

		/*
		 * Search the more recent half: [0; next),
		 * the optionals in this range must always be non-empty.
		 */
		if (auto *d = findInRange(first, mid))
			return d;

		/* Search the less recent half: [next_; capacity_) */
		if (mid != last && mid->has_value()) {
			/*
			 * If `next_` has wrapped around at least once, then all the optionals
			 * in [next_; capacity_) are non-empty. So if `*mid` is not empty,
			 * then all of them should be non-empty.
			 */
			if (auto *d = findInRange(mid, last))
				return d;
		}

		return nullptr;
	}

	T &allocNext(uint32_t frame)
	{
		if (!(lastFrame_ < frame)) {
			LOG(FCQueue, Fatal)
				<< "Tried to allocate frame context for frame " << frame
				<< " after having already allocated one for a later frame "
				<< *lastFrame_;
		}

		auto &e = entries_[next_].emplace(frame);
		LOG(FCQueue, Debug) << "frame " << frame << " slot " << next_;

		next_ = (next_ + 1) % capacity_;
		lastFrame_ = frame;

		return e.data;
	}

	std::unique_ptr<std::optional<Entry>[]> entries_;
	std::size_t capacity_;
	std::size_t next_ = 0;
	std::optional<uint32_t> lastFrame_;
};

} /* namespace ipa */

} /* namespace libcamera */
