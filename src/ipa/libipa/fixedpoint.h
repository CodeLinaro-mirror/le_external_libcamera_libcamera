/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * Copyright (C) 2024, Paul Elder <paul.elder@ideasonboard.com>
 *
 * Fixed / floating point conversions
 */

#pragma once

#include <cmath>
#include <type_traits>

#include "quantized.h"

namespace libcamera {

namespace ipa {

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

	if constexpr (std::is_unsigned_v<T>)
		return static_cast<R>(number) / static_cast<R>(1 << F);

	/*
	 * Recreate the upper bits in case of a negative number by shifting the sign
	 * bit from the fixed point to the first bit of the unsigned and then right shifting
	 * by the same amount which keeps the sign bit in place.
	 * This can be optimized by the compiler quite well.
	 */
	int remaining_bits = sizeof(int) * 8 - (I + F);
	int t = static_cast<int>(static_cast<unsigned>(number) << remaining_bits) >> remaining_bits;
	return static_cast<R>(t) / static_cast<R>(1 << F);
}

template<unsigned int I, unsigned int F, typename T>
struct FixedPointQTraits {
	static_assert(std::is_integral_v<T>, "FixedPointQTraits: T must be integral");
	using QuantizedType = T;
	using UT = std::make_unsigned_t<T>;

	static constexpr unsigned int Bits = I + F;
	static_assert(Bits <= sizeof(T) * 8, "FixedPointQTraits: too many bits for type T");

	static constexpr T BitMask = (Bits < sizeof(T) * 8)
				   ? static_cast<T>((UT{1} << Bits) - 1)
				   : static_cast<T>(~UT{0});

	static constexpr T qmin = std::is_signed_v<T>
				? static_cast<T>(-(UT{1} << (Bits - 1)))
				: static_cast<T>(0);

	static constexpr T qmax = std::is_signed_v<T>
				? static_cast<T>((UT{1} << (Bits - 1)) - 1)
				: static_cast<T>((UT{1} << Bits) - 1);

	static constexpr float toFloat(QuantizedType q)
	{
		return fixedToFloatingPoint<I, F, float, QuantizedType>(q);
	}

	static constexpr float min = fixedToFloatingPoint<I, F, float>(qmin);
	static constexpr float max = fixedToFloatingPoint<I, F, float>(qmax);

	/* Conversion functions required by Quantized<Traits> */
	static constexpr QuantizedType fromFloat(float v)
	{
		v = std::clamp(v, min, max);
		return floatingToFixedPoint<I, F, QuantizedType, float>(v);
	}
};

using Q1_7 = Quantized<FixedPointQTraits<1, 7, int8_t>>;
using UQ1_7 = Quantized<FixedPointQTraits<1, 7, uint8_t>>;

using UQ4_8 = Quantized<FixedPointQTraits<4, 8, uint16_t>>;

using Q5_4 = Quantized<FixedPointQTraits<5, 4, int16_t>>;
using UQ5_8 = Quantized<FixedPointQTraits<5, 8, uint16_t>>;

using Q12_4 = Quantized<FixedPointQTraits<12, 4, int16_t>>;
using UQ12_4 = Quantized<FixedPointQTraits<12, 4, uint16_t>>;

} /* namespace ipa */

} /* namespace libcamera */
