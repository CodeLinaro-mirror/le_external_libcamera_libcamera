/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * Copyright (C) 2025, Ideas On Board Oy
 *
 * Metadata list
 */

#pragma once

#include <algorithm>
#include <atomic>
#include <cassert>
#include <cstdint>
#include <optional>
#include <type_traits>
#include <utility>

#include <libcamera/base/internal/align.h>
#include <libcamera/base/internal/cxx20.h>
#include <libcamera/base/span.h>

#include <libcamera/controls.h>

namespace libcamera {

class MetadataListPlan;

class MetadataList
{
private:
	/**
	 * \brief The entry corresponding to a potential value in the list
	 */
	struct Entry {
		static constexpr uint32_t kInvalidOffset = -1;

		/**
		 * \brief Numeric identifier in the list
		 */
		const uint32_t tag;

		/**
		 * \brief Number of bytes available for the value
		 */
		const uint32_t capacity;

		/**
		 * \brief Alignment of the value
		 */
		const uint32_t alignment;

		const ControlType type;
		const bool isArray;

		/**
		 * \brief Offset of the ValueHeader of the value pertaining to this entry
		 *
		 * Offset from the beginning of the allocation, and
		 * and _not_ relative to `contentOffset_`.
		 */
		std::atomic_uint32_t headerOffset = kInvalidOffset;

		[[nodiscard]]
		std::optional<uint32_t> hasValue() const
		{
			auto offset = headerOffset.load(std::memory_order_relaxed);
			if (offset == kInvalidOffset)
				return {};

			return offset;
		}

		[[nodiscard]]
		std::optional<uint32_t> acquireData() const
		{
			auto offset = hasValue();
			if (offset) {
				/* sync with release-store on `headerOffset` in `MetadataList::set()` */
				std::atomic_thread_fence(std::memory_order_acquire);
			}

			return offset;
		}
	};

	/**
	 * \brief The header describing a value in the list
	 */
	struct ValueHeader {
		/**
		 * \brief Numeric identifier of the value in the list
		 */
		uint32_t tag;

		/**
		 * \brief Number of bytes used by the value
		 *
		 * This can be calculated using type and numElements, it is stored
		 * here to facilitate easier iteration in the buffer.
		 */
		uint32_t size;

		/**
		 * \brief Alignment of the value
		 */
		uint32_t alignment;

		/**
		 * \brief Type of the value
		 */
		ControlType type;

		/**
		 * \brief Whether the value is an array
		 */
		bool isArray;

		/**
		 * \brief Number of elements in the value
		 */
		uint32_t numElements;
	};

	struct State {
		/**
		 * \brief Number of items present in the list
		 */
		uint32_t count;

		/**
		 * \brief Number of bytes used in the buffer
		 */
		uint32_t fill;
	};

public:
	explicit MetadataList(const MetadataListPlan &plan);

	MetadataList(const MetadataList &) = delete;
	MetadataList(MetadataList &&) = delete;

	MetadataList &operator=(const MetadataList &) = delete;
	MetadataList &operator=(MetadataList &&) = delete;

	~MetadataList();

	// \todo want these?
	[[nodiscard]] std::size_t size() const { return state_.load(std::memory_order_relaxed).count; }
	[[nodiscard]] bool empty() const { return state_.load(std::memory_order_relaxed).fill == 0; }

	enum class SetError {
		UnknownTag = 1,
		AlreadySet,
		SizeMismatch,
		TypeMismatch,
	};

	[[nodiscard]]
	SetError set(uint32_t tag, ControlValueView v)
	{
		auto *e = find(tag);
		if (!e)
			return SetError::UnknownTag;

		return set(*e, v);
	}

	template<typename T>
	[[nodiscard]]
	SetError set(const Control<T> &ctrl, const internal::cxx20::type_identity_t<T> &value)
	{
		using TypeInfo = libcamera::details::control_type<T>;

		if constexpr (TypeInfo::size > 0) {
			static_assert(std::is_trivially_copyable_v<typename T::value_type>);

			return set(ctrl.id(), {
				TypeInfo::value,
				true,
				value.size(),
				reinterpret_cast<const std::byte *>(value.data()),
			});
		} else {
			static_assert(std::is_trivially_copyable_v<T>);

			return set(ctrl.id(), {
				TypeInfo::value,
				false,
				1,
				reinterpret_cast<const std::byte *>(&value),
			});
		}
	}

	template<typename T>
	[[nodiscard]]
	std::optional<T> get(const Control<T> &ctrl) const
	{
		ControlValueView v = get(ctrl.id());
		if (!v)
			return {};

		return v.get<T>();
	}

	// \todo operator ControlListView() const ?
	// \todo explicit operator ControlList() const ?

	[[nodiscard]]
	ControlValueView get(uint32_t tag) const
	{
		const auto *e = find(tag);
		if (!e)
			return {};

		return dataOf(*e);
	}

	void clear();

	class iterator
	{
	public:
		using difference_type = std::ptrdiff_t;
		using value_type = std::pair<uint32_t, ControlValueView>;
		using pointer = void;
		using reference = value_type;
		using iterator_category = std::forward_iterator_tag;

		iterator() = default;

		iterator& operator++()
		{
			const auto &h = header();

			p_ += sizeof(h);
			p_ = internal::align::up(p_, h.alignment);
			p_ += h.size;
			p_ = internal::align::up(p_, alignof(decltype(h)));

			return *this;
		}

		iterator operator++(int)
		{
			auto copy = *this;
			++*this;
			return copy;
		}

		[[nodiscard]]
		reference operator*() const
		{
			const auto &h = header();
			const auto *data = internal::align::up(p_ + sizeof(h), h.alignment);

			return { h.tag, { h.type, h.isArray, h.numElements, data } };
		}

		[[nodiscard]]
		bool operator==(const iterator &other) const
		{
			return p_ == other.p_;
		}

		[[nodiscard]]
		bool operator!=(const iterator &other) const
		{
			return !(*this == other);
		}

	private:
		iterator(const std::byte *p)
			: p_(p)
		{
		}

		[[nodiscard]]
		const ValueHeader &header() const
		{
			return *reinterpret_cast<const ValueHeader *>(p_);
		}

		friend MetadataList;

		const std::byte *p_ = nullptr;
	};

	[[nodiscard]]
	iterator begin() const
	{
		return { p_ + contentOffset_ };
	}

	[[nodiscard]]
	iterator end() const
	{
		return { p_ + contentOffset_ + state_.load(std::memory_order_acquire).fill };
	}

	class Diff
	{
	public:
		// \todo want these?
		[[nodiscard]] explicit operator bool() const { return !empty(); }
		[[nodiscard]] bool empty() const { return start_ == stop_; }
		[[nodiscard]] std::size_t size() const { return changed_; }
		[[nodiscard]] const MetadataList &list() const { return *list_; }

		[[nodiscard]]
		ControlValueView get(uint32_t tag) const
		{
			const auto *e = list_->find(tag);
			if (!e)
				return {};

			auto o = e->acquireData();
			if (!o)
				return {};

			if (!(start_ <= *o && *o < stop_))
				return {};

			return list_->dataOf(*o);
		}

		template<typename T>
		[[nodiscard]]
		std::optional<T> get(const Control<T> &ctrl) const
		{
			ControlValueView v = get(ctrl.id());
			if (!v)
				return {};

			return v.get<T>();
		}

		[[nodiscard]]
		iterator begin() const
		{
			return { list_->p_ + start_ };
		}

		[[nodiscard]]
		iterator end() const
		{
			return { list_->p_ + stop_ };
		}

	private:
		Diff(const MetadataList &list, std::size_t changed, std::size_t oldFill, std::size_t newFill)
			: list_(&list),
			  changed_(changed),
			  start_(list.contentOffset_ + oldFill),
			  stop_(list.contentOffset_ + newFill)
		{
		}

		friend MetadataList;
		friend struct Checkpoint;

		/**
		 * \brief Source lits of the checkpoint
		 */
		const MetadataList *list_ = nullptr;

		/**
		 * \brief Number of items contained in the diff
		 */
		std::size_t changed_;

		/**
		 * \brief Offset of the ValueHeader of the first value in the diff
		 */
		std::size_t start_;

		/**
		 * \brief Offset of the "past-the-end" ValueHeader of the diff
		 */
		std::size_t stop_;
	};

	[[nodiscard]] std::optional<Diff> merge(const ControlList &other);

	class Checkpoint
	{
	public:
		[[nodiscard]]
		Diff diffSince() const
		{
			/* sync with release-store on `state_` in `set()` */
			const auto curr = list_->state_.load(std::memory_order_acquire);

			assert(state_.count <= curr.count);
			assert(state_.fill <= curr.fill);

			return {
				*list_,
				curr.count - state_.count,
				state_.fill,
				curr.fill,
			};
		}

	private:
		Checkpoint(const MetadataList &list)
			: list_(&list),
			  state_(list.state_.load(std::memory_order_relaxed))
		{
		}

		friend MetadataList;

		/**
		 * \brief Source list of the checkpoint
		 */
		const MetadataList *list_ = nullptr;

		/**
		 * \brief State of the list when the checkpoint was created
		 */
		State state_ = {};
	};

	[[nodiscard]]
	Checkpoint checkpoint() const
	{
		return { *this };
	}

private:
	[[nodiscard]]
	static constexpr std::size_t entriesOffset()
	{
		return 0;
	}

	[[nodiscard]]
	static constexpr std::size_t contentOffset(std::size_t entries)
	{
		return internal::align::up(entriesOffset() + entries * sizeof(Entry), alignof(ValueHeader));
	}

	[[nodiscard]]
	Span<Entry> entries() const
	{
		return { reinterpret_cast<Entry *>(p_ + entriesOffset()), capacity_ };
	}

	[[nodiscard]]
	Entry *find(uint32_t tag) const
	{
		const auto entries = this->entries();
		auto it = std::partition_point(entries.begin(), entries.end(), [&](const auto &e) {
			return e.tag < tag;
		});

		if (it == entries.end() || it->tag != tag)
			return nullptr;

		return &*it;
	}

	[[nodiscard]]
	ControlValueView dataOf(const Entry &e) const
	{
		const auto o = e.acquireData();
		return o ? dataOf(*o) : ControlValueView{ };
	}

	[[nodiscard]]
	ControlValueView dataOf(std::size_t headerOffset) const
	{
		assert(headerOffset <= alloc_ - sizeof(ValueHeader));
		assert(internal::align::is(p_ + headerOffset, alignof(ValueHeader)));

		const auto *vh = reinterpret_cast<const ValueHeader *>(p_ + headerOffset);
		const auto *p = reinterpret_cast<const std::byte *>(vh) + sizeof(*vh);
		std::size_t avail = p_ + alloc_ - p;

		const auto *data = internal::align::up(vh->size, vh->alignment, p, &avail);
		assert(data);

		return { vh->type, vh->isArray, vh->numElements, data };
	}

	[[nodiscard]] SetError set(Entry &e, ControlValueView v);

	[[nodiscard]]
	std::pair<MetadataList::SetError, MetadataList::ValueHeader *>
	set(const Entry &e, ControlValueView v, State &s);

	/**
	 * \brief Number of \ref Entry "entries"
	 */
	std::size_t capacity_ = 0;

	/**
	 * \brief Offset of the first ValueHeader
	 */
	std::size_t contentOffset_ = -1;

	/**
	 * \brief Pointer to the allocation
	 */
	std::byte *p_ = nullptr;

	/**
	 * \brief Size of the allocation in bytes
	 */
	std::size_t alloc_ = 0;

	/**
	 * \brief Current state of the list
	 */
	std::atomic<State> state_ = State{};

	// \todo ControlIdMap in any way shape or form?

	/*
	 * If this is problematic on a 32-bit architecture, then
	 * `count` can be stored in a separate atomic variable
	 * but then `Diff::changed_` must be removed since the fill
	 * level and item count cannot be retrieved atomically.
	 */
	static_assert(decltype(state_)::is_always_lock_free);
};

} /* namespace libcamera */
