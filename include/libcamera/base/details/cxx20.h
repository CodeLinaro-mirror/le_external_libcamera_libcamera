/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * Copyright (C) 2025, Ideas on Board Oy
 *
 * C++20 polyfills
 */

#pragma once

#include <type_traits>

namespace libcamera::details::cxx20 {

template<typename T> struct type_identity { using type = T; };
template<typename T> using type_identity_t = typename type_identity<T>::type;

template<typename T>
constexpr bool has_single_bit(T x) noexcept
{
	static_assert(std::is_unsigned_v<T>);

	return x != 0 && (x & (x - 1)) == 0;
}

} /* namespace libcamera::details::cxx20 */
