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
#include <cstring>
#include <new>
#include <optional>
#include <type_traits>

#include <libcamera/base/details/align.h>
#include <libcamera/base/details/cxx20.h>
#include <libcamera/base/span.h>

#include <libcamera/controls.h>
#include <libcamera/metadata_list_plan.h>

// TODO: want this?
#if __has_include(<sanitizer/asan_interface.h>)
#if __SANITIZE_ADDRESS__ /* gcc */
#include <sanitizer/asan_interface.h>
#define HAS_ASAN 1
#elif defined(__has_feature)
#if __has_feature(address_sanitizer) /* clang */
#include <sanitizer/asan_interface.h>
#define HAS_ASAN 1
#endif
#endif
#endif

namespace libcamera {

class MetadataList
{
private:
	struct ValueParams {
		ControlType type;
		bool isArray;
		std::uint32_t numElements;
	};

	struct Entry {
		const std::uint32_t tag;
		const std::uint32_t capacity;
		const std::uint32_t alignment;
		const ControlType type;
		bool isArray;

		static constexpr std::uint32_t invalidOffset = -1;
		/*
		 * Offset from the beginning of the allocation, and
		 * and _not_ relative to `contentOffset_`.
		 */
		std::atomic_uint32_t headerOffset = invalidOffset;

		[[nodiscard]] std::optional<std::uint32_t> hasValue() const
		{
			auto offset = headerOffset.load(std::memory_order_relaxed);
			if (offset == invalidOffset)
				return {};

			return offset;
		}

		[[nodiscard]] std::optional<std::uint32_t> acquireData() const
		{
			auto offset = hasValue();
			if (offset) {
				/* sync with release-store on `headerOffset` in `MetadataList::set()` */
				std::atomic_thread_fence(std::memory_order_acquire);
			}

			return offset;
		}
	};

	struct ValueHeader {
		std::uint32_t tag;
		std::uint32_t size;
		std::uint32_t alignment;
		ValueParams params;
	};

	struct State {
		std::uint32_t count;
		std::uint32_t fill;
	};

public:
	explicit MetadataList(const MetadataListPlan &plan)
		: capacity_(plan.size()),
		  contentOffset_(MetadataList::contentOffset(capacity_)),
		  alloc_(contentOffset_)
	{
		for (const auto &[tag, e] : plan) {
			alloc_ += sizeof(ValueHeader);
			alloc_ += e.alignment - 1; // XXX: this is the maximum
			alloc_ += e.size * e.numElements;
			alloc_ += alignof(ValueHeader) - 1; // XXX: this is the maximum
		}

		p_ = static_cast<std::byte *>(::operator new(alloc_));

		auto *entries = reinterpret_cast<Entry *>(p_ + entriesOffset());
		auto it = plan.begin();

		for (std::size_t i = 0; i < capacity_; i++, ++it) {
			const auto &[tag, e] = *it;

			new (&entries[i]) Entry{
				.tag = tag,
				.capacity = e.size * e.numElements,
				.alignment = e.alignment,
				.type = e.type,
				.isArray = e.isArray,
			};
		}

#if HAS_ASAN
		::__sanitizer_annotate_contiguous_container(
			p_ + contentOffset_, p_ + alloc_,
			p_ + alloc_, p_ + contentOffset_
		);
#endif
	}

	MetadataList(const MetadataList &) = delete;
	MetadataList(MetadataList &&) = delete;

	MetadataList &operator=(const MetadataList &) = delete;
	MetadataList &operator=(MetadataList &&) = delete;

	~MetadataList()
	{
#if HAS_ASAN
		/*
		 * The documentation says the range apparently has to be
		 * restored to its initial state before it is deallocated.
		 */
		::__sanitizer_annotate_contiguous_container(
			p_ + contentOffset_, p_ + alloc_,
			p_ + contentOffset_ + state_.load(std::memory_order_relaxed).fill, p_ + alloc_
		);
#endif

		::operator delete(p_, alloc_);
	}

	// TODO: want these?
	[[nodiscard]] std::size_t size() const { return state_.load(std::memory_order_relaxed).count; }
	[[nodiscard]] bool empty() const { return state_.load(std::memory_order_relaxed).fill == 0; }

	enum class SetError {
		UnknownTag = 1,
		AlreadySet,
		SizeMismatch,
		TypeMismatch,
	};

	[[nodiscard]] SetError set(std::uint32_t tag, ControlValueView v)
	{
		auto *e = find(tag);
		if (!e)
			return SetError::UnknownTag;

		return set(*e, v);
	}

	template<typename T>
	/* TODO: [[nodiscard]] */ SetError set(const Control<T> &ctrl, const details::cxx20::type_identity_t<T> &value)
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
	[[nodiscard]] decltype(auto) get(const Control<T> &ctrl) const
	{
		ControlValueView v = get(ctrl.id());

		return v ? std::optional(v.get<T>()) : std::nullopt;
	}

	// TODO: operator ControlListView() const ?
	// TODO: explicit operator ControlList() const ?

	[[nodiscard]] ControlValueView get(std::uint32_t tag) const
	{
		const auto *e = find(tag);
		if (!e)
			return {};

		return data_of(*e);
	}

	void clear()
	{
		for (auto &e : entries())
			e.headerOffset.store(Entry::invalidOffset, std::memory_order_relaxed);

		[[maybe_unused]] auto s = state_.exchange({}, std::memory_order_relaxed);

#if HAS_ASAN
		::__sanitizer_annotate_contiguous_container(
			p_ + contentOffset_, p_ + alloc_,
			p_ + contentOffset_ + s.fill, p_ + contentOffset_
		);
#endif
	}

	class iterator
	{
	public:
		using difference_type = std::ptrdiff_t;
		using value_type = std::pair<std::uint32_t, ControlValueView>;
		using pointer = void;
		using reference = value_type;
		using iterator_category = std::forward_iterator_tag;

		iterator() = default;

		iterator& operator++()
		{
			const auto &h = header();

			p_ += sizeof(h);
			p_ = details::align::up(p_, h.alignment);
			p_ += h.size;
			p_ = details::align::up(p_, alignof(decltype(h)));

			return *this;
		}

		iterator operator++(int)
		{
			auto copy = *this;
			++*this;
			return copy;
		}

		[[nodiscard]] reference operator*() const
		{
			const auto &h = header();
			const auto *data = details::align::up(p_ + sizeof(h), h.alignment);

			return { h.tag, { h.params.type, h.params.isArray, h.params.numElements, data } };
		}

		[[nodiscard]] bool operator==(const iterator &other) const
		{
			return p_ == other.p_;
		}

		[[nodiscard]] bool operator!=(const iterator &other) const
		{
			return !(*this == other);
		}

	private:
		iterator(const std::byte *p)
			: p_(p)
		{
		}

		[[nodiscard]] const ValueHeader &header() const
		{
			return *reinterpret_cast<const ValueHeader *>(p_);
		}

		friend MetadataList;

		const std::byte *p_ = nullptr;
	};

	[[nodiscard]] iterator begin() const
	{
		return { p_ + contentOffset_ };
	}

	[[nodiscard]] iterator end() const
	{
		return { p_ + contentOffset_ + state_.load(std::memory_order_acquire).fill };
	}

	class Diff
	{
	public:
		// TODO: want these?
		[[nodiscard]] explicit operator bool() const { return !empty(); }
		[[nodiscard]] bool empty() const { return start_ == stop_; }
		[[nodiscard]] std::size_t size() const { return changed_; }
		[[nodiscard]] const MetadataList &list() const { return *l_; }

		[[nodiscard]] ControlValueView get(std::uint32_t tag) const
		{
			const auto *e = l_->find(tag);
			if (!e)
				return {};

			auto o = e->acquireData();
			if (!o)
				return {};

			if (!(start_ <= *o && *o < stop_))
				return {};

			return l_->data_of(*o);
		}

		template<typename T>
		[[nodiscard]] decltype(auto) get(const Control<T> &ctrl) const
		{
			ControlValueView v = get(ctrl.id());

			return v ? std::optional(v.get<T>()) : std::nullopt;
		}

		[[nodiscard]] iterator begin() const
		{
			return { l_->p_ + start_ };
		}

		[[nodiscard]] iterator end() const
		{
			return { l_->p_ + stop_ };
		}

	private:
		Diff(const MetadataList &l, std::size_t changed, std::size_t oldFill, std::size_t newFill)
			: l_(&l),
			  changed_(changed),
			  start_(l.contentOffset_ + oldFill),
			  stop_(l.contentOffset_ + newFill)
		{
		}

		friend MetadataList;
		friend struct Checkpoint;

		const MetadataList *l_ = nullptr;
		std::size_t changed_;
		std::size_t start_;
		std::size_t stop_;
	};

	Diff merge(const ControlList &other)
	{
		// TODO: check id map of `other`?

		const auto c = checkpoint();

		for (const auto &[tag, value] : other) {
			auto *e = find(tag);
			if (e) {
				[[maybe_unused]] auto r = set(*e, value);
				assert(r == SetError() || r == SetError::AlreadySet); // TODO: ?
			}
		}

		return c.diffSince();
	}

	class Checkpoint
	{
	public:
		[[nodiscard]] Diff diffSince() const
		{
			/* sync with release-store on `state_` in `set()` */
			const auto curr = l_->state_.load(std::memory_order_acquire);

			assert(s_.count <= curr.count);
			assert(s_.fill <= curr.fill);

			return {
				*l_,
				curr.count - s_.count,
				s_.fill,
				curr.fill,
			};
		}

	private:
		Checkpoint(const MetadataList &l)
			: l_(&l),
			  s_(l.state_.load(std::memory_order_relaxed))
		{
		}

		friend MetadataList;

		const MetadataList *l_ = nullptr;
		State s_ = {};
	};

	[[nodiscard]] Checkpoint checkpoint() const
	{
		return { *this };
	}

private:
	[[nodiscard]] static constexpr std::size_t entriesOffset()
	{
		return 0;
	}

	[[nodiscard]] static constexpr std::size_t contentOffset(std::size_t entries)
	{
		return details::align::up(entriesOffset() + entries * sizeof(Entry), alignof(ValueHeader));
	}

	[[nodiscard]] Span<Entry> entries() const
	{
		return { reinterpret_cast<Entry *>(p_ + entriesOffset()), capacity_ };
	}

	[[nodiscard]] Entry *find(std::uint32_t tag) const
	{
		const auto entries = this->entries();
		auto it = std::partition_point(entries.begin(), entries.end(), [&](const auto &e) {
			return e.tag < tag;
		});

		if (it == entries.end() || it->tag != tag)
			return nullptr;

		return &*it;
	}

	[[nodiscard]] ControlValueView data_of(const Entry &e) const
	{
		const auto o = e.acquireData();
		return o ? data_of(*o) : ControlValueView{ };
	}

	[[nodiscard]] ControlValueView data_of(std::size_t headerOffset) const
	{
		assert(headerOffset <= alloc_ - sizeof(ValueHeader));
		assert(details::align::is(p_ + headerOffset, alignof(ValueHeader)));

		const auto *vh = reinterpret_cast<const ValueHeader *>(p_ + headerOffset);
		const auto *p = reinterpret_cast<const std::byte *>(vh) + sizeof(*vh);
		std::size_t avail = p_ + alloc_ - p;

		const auto *data = details::align::up(vh->size, vh->alignment, p, &avail);
		assert(data);

		return { vh->params.type, vh->params.isArray, vh->params.numElements, data };
	}

	[[nodiscard]] SetError set(Entry &e, ControlValueView v)
	{
		if (e.hasValue())
			return SetError::AlreadySet;
		if (e.type != v.type() || e.isArray != v.isArray())
			return SetError::TypeMismatch;

		const auto src = v.data();
		if (e.isArray) {
			if (src.size_bytes() > e.capacity)
				return SetError::SizeMismatch;
		} else {
			if (src.size_bytes() != e.capacity)
				return SetError::SizeMismatch;
		}

		auto s = state_.load(std::memory_order_relaxed);
		std::byte *oldEnd = p_ + contentOffset_ + s.fill;
		std::byte *p = oldEnd;

		auto *headerPtr = details::align::up<ValueHeader>(p);
		auto *dataPtr = details::align::up(src.size_bytes(), e.alignment, p);
		details::align::up(0, alignof(ValueHeader), p);

#if HAS_ASAN
		::__sanitizer_annotate_contiguous_container(
			p_ + contentOffset_, p_ + alloc_,
			oldEnd, p
		);
#endif

		new (headerPtr) ValueHeader{
			.tag = e.tag,
			.size = std::uint32_t(src.size_bytes()),
			.alignment = e.alignment,
			.params = {
				.type = v.type(),
				.isArray = v.isArray(),
				.numElements = std::uint32_t(v.numElements()),
			},
		};
		std::memcpy(dataPtr, src.data(), src.size_bytes());
		e.headerOffset.store(reinterpret_cast<std::byte *>(headerPtr) - p_, std::memory_order_release);

		s.fill += p - oldEnd;
		s.count += 1;

		state_.store(s, std::memory_order_release);

		return {};
	}

	std::size_t capacity_ = 0;
	std::size_t contentOffset_ = -1;
	std::size_t alloc_ = 0;
	std::atomic<State> state_ = State{};
	std::byte *p_ = nullptr;
	// TODO: ControlIdMap in any way shape or form?

	/*
	 * If this is problematic on a 32-bit architecture, then
	 * `count` can be stored in a separate atomic variable
	 * but then `Diff::changed_` must be removed since the fill
	 * level and item count cannot be retrieved atomically.
	 */
	static_assert(decltype(state_)::is_always_lock_free);
};

} /* namespace libcamera */

#undef HAS_ASAN
