/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * Copyright (C) 2025, Ideas on Board Oy
 *
 * Alignment utilities
 */

#pragma once

#include <cassert>
#include <cstddef>
#include <cstdint>

#include <libcamera/base/details/cxx20.h>

namespace libcamera::details::align {

inline bool is(const void *p, std::uintptr_t alignment)
{
	assert(alignment > 0);

	return reinterpret_cast<std::uintptr_t>(p) % alignment == 0;
}

template<typename T>
constexpr T up(T x, cxx20::type_identity_t<T> alignment)
{
	static_assert(std::is_unsigned_v<T>);
	assert(alignment > 0);

	const auto padding = (alignment - (x % alignment)) % alignment;
	assert(x + padding >= x);

	return x + padding;
}

template<typename T>
auto *up(T *p, std::uintptr_t alignment)
{
	using U = std::conditional_t<
		std::is_const_v<T>,
		const std::byte,
		std::byte
	>;

	return reinterpret_cast<U *>(up(reinterpret_cast<std::uintptr_t>(p), alignment));
}

template<typename T>
T *up(std::size_t size, std::size_t alignment, T *&ptr, std::size_t *avail = nullptr)
{
	assert(alignment > 0);

	auto p = reinterpret_cast<std::uintptr_t>(ptr);
	const auto padding = (alignment - (p % alignment)) % alignment;

	if (avail) {
		if (size > *avail || padding > *avail - size)
			return nullptr;

		*avail -= size + padding;
	}

	p += padding;
	ptr = reinterpret_cast<T *>(p + size);

	return reinterpret_cast<T *>(p);
}

template<typename U, typename T>
U *up(T *&ptr, std::size_t *avail = nullptr)
{
	return reinterpret_cast<std::conditional_t<std::is_const_v<T>, const U, U> *>(
		up(sizeof(U), alignof(U), ptr, avail)
	);
}

} /* namespace libcamera::details::align */
