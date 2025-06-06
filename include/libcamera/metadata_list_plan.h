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
	[[nodiscard]] bool empty() const { return items_.empty(); }
	[[nodiscard]] std::size_t size() const { return items_.size(); }
	[[nodiscard]] decltype(auto) begin() const { return items_.begin(); }
	[[nodiscard]] decltype(auto) end() const { return items_.end(); }
	void clear() { items_.clear(); }

	template<
		typename T,
		std::enable_if_t<libcamera::details::control_type<T>::size != libcamera::dynamic_extent> * = nullptr
	>
	decltype(auto) add(const Control<T> &ctrl)
	{
		if constexpr (libcamera::details::control_type<T>::size > 0) {
			static_assert(libcamera::details::control_type<T>::size != libcamera::dynamic_extent);

			return add<typename T::value_type>(
				ctrl.id(),
				libcamera::details::control_type<T>::size,
				true
			);
		} else {
			return add<T>(ctrl.id(), 1, false);
		}
	}

	template<
		typename T,
		std::enable_if_t<libcamera::details::control_type<T>::size == libcamera::dynamic_extent> * = nullptr
	>
	decltype(auto) add(const Control<T> &ctrl, std::size_t count)
	{
		return add<typename T::value_type>(ctrl.id(), count, true);
	}

#ifndef __DOXYGEN__
	MetadataListPlan &add(std::uint32_t tag,
			      std::size_t size, std::size_t count, std::size_t alignment,
			      ControlType type, bool isArray)
	{
		assert(size > 0 && size <= std::numeric_limits<std::uint32_t>::max());
		assert(count <= std::numeric_limits<std::uint32_t>::max() / size);
		assert(alignment <= std::numeric_limits<std::uint32_t>::max());
		assert(details::cxx20::has_single_bit(alignment));
		assert(isArray || count == 1);

		items_.insert_or_assign(tag, Entry{
			.size = std::uint32_t(size * count),
			.alignment = std::uint32_t(alignment),
			.type = type,
			.isArray = isArray,
		});

		return *this;
	}
#endif

	bool remove(std::uint32_t tag)
	{
		return items_.erase(tag);
	}

	bool remove(const ControlId &ctrl)
	{
		return remove(ctrl.id());
	}

private:
	struct Entry {
		std::uint32_t size;
		std::uint32_t alignment; // TODO: is this necessary?
		ControlType type;
		bool isArray;
	};

	std::map<std::uint32_t, Entry> items_;

	template<typename T>
	decltype(auto) add(std::uint32_t tag, std::size_t count, bool isArray)
	{
		static_assert(std::is_trivially_copyable_v<T>);

		return add(tag, sizeof(T), count, alignof(T), details::control_type<T>::value, isArray);
	}
};

} /* namespace libcamera */
