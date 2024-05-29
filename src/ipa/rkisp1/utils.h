/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * Copyright (C) 2024, Paul Elder <paul.elder@ideasonboard.com>
 *
 * Miscellaneous utility functions specific to rkisp1
 */

#pragma once

#include <cmath>
#include <limits>
#include <type_traits>

namespace libcamera {

namespace ipa::rkisp1::utils {

#ifndef __DOXYGEN__
template<unsigned int I, unsigned int F, typename R, typename T,
	 std::enable_if_t<std::is_integral_v<R> &&
			  std::is_floating_point_v<T>> * = nullptr>
#else
template<unsigned int I, unsigned int F, typename R, typename T>
#endif
constexpr R floatingToFixedPoint(T number)
{
	static_assert(sizeof(int) >= sizeof(R));
	static_assert(I + F <= sizeof(R) * 8);

	/*
	 * The intermediate cast to int is needed on arm platforms to properly
	 * cast negative values. See
	 * https://embeddeduse.com/2013/08/25/casting-a-negative-float-to-an-unsigned-int/
	 */
	R mask = (1 << (F + I)) - 1;
	R frac = static_cast<R>(static_cast<int>(std::round(number * (1 << F)))) & mask;

	return frac;
}

#ifndef __DOXYGEN__
template<unsigned int I, unsigned int F, typename R, typename T,
	 std::enable_if_t<std::is_floating_point_v<R> &&
			  std::is_integral_v<T>> * = nullptr>
#else
template<unsigned int I, unsigned int F, typename R, typename T>
#endif
constexpr R fixedToFloatingPoint(T number)
{
	static_assert(sizeof(int) >= sizeof(T));
	static_assert(I + F <= sizeof(T) * 8);

	/*
	 * Create a number with all upper bits set including the sign bit
	 * of the fixed point number. We need an extra cast as the compiler
	 * won't let us left-shift negative numbers.
	 */
	int upper_ones = static_cast<int>(std::numeric_limits<unsigned int>::max() << (I + F - 1));
	if (number & upper_ones)
		return static_cast<R>(upper_ones | number) / static_cast<R>(1 << F);

	return static_cast<R>(number) / static_cast<R>(1 << F);
}

} /* namespace ipa::rkisp1::utils */

} /* namespace libcamera */
