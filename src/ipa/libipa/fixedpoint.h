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
		if constexpr (std::is_unsigned_v<T>)
			return static_cast<float>(q) / static_cast<float>(1 << F);

		/*
		 * Recreate the upper bits in case of a negative number by shifting the sign
		 * bit from the fixed point to the first bit of the unsigned and then right shifting
		 * by the same amount which keeps the sign bit in place.
		 * This can be optimized by the compiler quite well.
		 */
		static_assert(sizeof(int) >= sizeof(T));

		int remaining_bits = sizeof(int) * 8 - (I + F);
		int t = static_cast<int>(static_cast<unsigned>(q) << remaining_bits) >> remaining_bits;
		return static_cast<float>(t) / static_cast<float>(1 << F);
	}

	static constexpr float min = toFloat(qmin);
	static constexpr float max = toFloat(qmax);

	/* Conversion functions required by Quantized<Traits> */
	static constexpr QuantizedType fromFloat(float v)
	{
		v = std::clamp(v, min, max);

		/*
		 * The intermediate cast to int is needed on arm platforms to properly
		 * cast negative values. See
		 * https://embeddeduse.com/2013/08/25/casting-a-negative-float-to-an-unsigned-int/
		 */
		return static_cast<T>(static_cast<int>(std::round(v * (1 << F)))) & BitMask;
	}
};

using Q1_7 = Quantized<FixedPointQTraits<1, 7, int8_t>>;
using UQ1_7 = Quantized<FixedPointQTraits<1, 7, uint8_t>>;

using UQ4_8 = Quantized<FixedPointQTraits<4, 8, uint16_t>>;

using Q5_4 = Quantized<FixedPointQTraits<5, 4, int16_t>>;
using UQ5_8 = Quantized<FixedPointQTraits<5, 8, uint16_t>>;

using Q12_4 = Quantized<FixedPointQTraits<12, 4, int16_t>>;
using UQ12_4 = Quantized<FixedPointQTraits<12, 4, uint16_t>>;

template<typename Q, int Scale>
struct ScaledFixedPointQTraits {
	using QuantizedType = typename Q::QuantizedType;

	static constexpr float scale = static_cast<float>(Scale);

	/* Re-expose base limits, adjusted by the scaling factor */
	static constexpr QuantizedType qmin = Q::qmin;
	static constexpr QuantizedType qmax = Q::qmax;
	static constexpr float min = Q::min * scale;
	static constexpr float max = Q::max * scale;

	static QuantizedType fromFloat(float v)
	{
		v = std::clamp(v, min, max);
		return Q::fromFloat(v / scale);
	}

	static float toFloat(QuantizedType q)
	{
		return Q::toFloat(q) * scale;
	}
};

} /* namespace ipa */

} /* namespace libcamera */
