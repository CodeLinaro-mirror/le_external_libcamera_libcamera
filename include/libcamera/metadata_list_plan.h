/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * Copyright (C) 2025, Ideas On Board Oy
 */

#pragma once

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <map>
#include <type_traits>

#include <libcamera/base/details/cxx20.h>

#include <libcamera/controls.h>

namespace libcamera {

class MetadataListPlan
{
public:
	struct Entry {
		std::uint32_t size;
		std::uint32_t alignment; // TODO: is this necessary?
		std::uint32_t numElements;
		ControlType type;
		bool isArray;
	};

	[[nodiscard]] bool empty() const { return items_.empty(); }
	[[nodiscard]] std::size_t size() const { return items_.size(); }
	[[nodiscard]] decltype(auto) begin() const { return items_.begin(); }
	[[nodiscard]] decltype(auto) end() const { return items_.end(); }
	void clear() { items_.clear(); }

	template<
		typename T,
		std::enable_if_t<libcamera::details::control_type<T>::size != libcamera::dynamic_extent> * = nullptr
	>
	decltype(auto) set(const Control<T> &ctrl)
	{
		if constexpr (libcamera::details::control_type<T>::size > 0) {
			static_assert(libcamera::details::control_type<T>::size != libcamera::dynamic_extent);

			return set<typename T::value_type>(
				ctrl.id(),
				libcamera::details::control_type<T>::size,
				true
			);
		} else {
			return set<T>(ctrl.id(), 1, false);
		}
	}

	template<
		typename T,
		std::enable_if_t<libcamera::details::control_type<T>::size == libcamera::dynamic_extent> * = nullptr
	>
	decltype(auto) set(const Control<T> &ctrl, std::size_t numElements)
	{
		return set<typename T::value_type>(ctrl.id(), numElements, true);
	}

	[[nodiscard]] bool set(std::uint32_t tag,
			       std::size_t size, std::size_t alignment,
			       std::size_t numElements, ControlType type, bool isArray)
	{
		if (size == 0 || size > std::numeric_limits<std::uint32_t>::max())
			return false;
		if (alignment > std::numeric_limits<std::uint32_t>::max())
			return false;
		if (!details::cxx20::has_single_bit(alignment))
			return false;
		if (numElements > std::numeric_limits<std::uint32_t>::max() / size)
			return false;
		if (!isArray && numElements != 1)
			return false;

		items_.insert_or_assign(tag, Entry{
			.size = std::uint32_t(size),
			.alignment = std::uint32_t(alignment),
			.numElements = std::uint32_t(numElements),
			.type = type,
			.isArray = isArray,
		});

		return true;
	}

	[[nodiscard]] const Entry *get(std::uint32_t tag) const
	{
		auto it = items_.find(tag);
		if (it == items_.end())
			return nullptr;

		return &it->second;
	}

	[[nodiscard]] const Entry *get(const ControlId &cid) const
	{
		const auto *e = get(cid.id());
		if (!e)
			return nullptr;

		if (e->type != cid.type() || e->isArray != cid.isArray())
			return nullptr;

		return e;
	}

private:
	std::map<std::uint32_t, Entry> items_;

	template<typename T>
	decltype(auto) set(std::uint32_t tag, std::size_t numElements, bool isArray)
	{
		static_assert(std::is_trivially_copyable_v<T>);

		[[maybe_unused]] bool ok = set(tag,
					       sizeof(T), alignof(T),
					       numElements, details::control_type<T>::value, isArray);
		assert(ok);

		return *this;
	}
};

} /* namespace libcamera */
