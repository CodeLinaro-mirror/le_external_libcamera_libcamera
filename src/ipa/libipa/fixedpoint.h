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

template<unsigned int I, unsigned int F, typename T>
struct FixedPointQTraits {
private:
	static_assert(std::is_integral_v<T>, "FixedPointQTraits: T must be integral");
	using UT = std::make_unsigned_t<T>;

	static constexpr unsigned int bits = I + F;
	static_assert(bits <= sizeof(T) * 8, "FixedPointQTraits: too many bits for type T");

	static constexpr T bitMask = (bits < sizeof(T) * 8)
				   ? static_cast<T>((UT{1} << bits) - 1)
				   : static_cast<T>(~UT{0});

public:
	using QuantizedType = T;

	static constexpr T qMin = std::is_signed_v<T>
				? static_cast<T>(-(UT{1} << (bits - 1)))
				: static_cast<T>(0);

	static constexpr T qMax = std::is_signed_v<T>
				? static_cast<T>((UT{1} << (bits - 1)) - 1)
				: static_cast<T>((UT{1} << bits) - 1);

	static constexpr float toFloat(QuantizedType q)
	{
		if constexpr (std::is_unsigned_v<T>)
			return static_cast<float>(q) / static_cast<float>(1 << F);

		/*
		 * Recreate the upper bits in case of a negative number by
		 * shifting the sign bit from the fixed point to the first bit
		 * of the unsigned and then right shifting by the same amount
		 * which keeps the sign bit in place. This can be optimized by
		 * the compiler quite well.
		 */
		static_assert(sizeof(int) >= sizeof(T));

		int remaining_bits = sizeof(int) * 8 - (I + F);
		int t = static_cast<int>(static_cast<unsigned>(q) << remaining_bits) >> remaining_bits;
		return static_cast<float>(t) / static_cast<float>(1 << F);
	}

	static constexpr float min = toFloat(qMin);
	static constexpr float max = toFloat(qMax);

	static_assert(min < max, "FixedPointQTraits: Minimum must be less than maximum");

	/* Conversion functions required by Quantized<Traits> */
	static QuantizedType fromFloat(float v)
	{
		v = std::clamp(v, min, max);

		/*
		 * The intermediate cast to int is needed on arm platforms to
		 * properly cast negative values. See
		 * https://embeddeduse.com/2013/08/25/casting-a-negative-float-to-an-unsigned-int/
		 */
		return static_cast<T>(static_cast<int>(std::round(v * (1 << F)))) & bitMask;
	}
};

namespace details {

template<unsigned int Bits>
constexpr auto qtype()
{
	static_assert(Bits <= 64);

	if constexpr (Bits <= 8)
		return int8_t();
	else if constexpr (Bits <= 16)
		return int16_t();
	else if constexpr (Bits <= 32)
		return int32_t();
	else if constexpr (Bits <= 64)
		return int64_t();
}

} /* namespace details */

template<unsigned int I, unsigned int F>
using Q = Quantized<FixedPointQTraits<I, F, decltype(details::qtype<I + F>())>>;

template<unsigned int I, unsigned int F>
using UQ = Quantized<FixedPointQTraits<I, F, std::make_unsigned_t<decltype(details::qtype<I + F>())>>>;

} /* namespace ipa */

} /* namespace libcamera */
