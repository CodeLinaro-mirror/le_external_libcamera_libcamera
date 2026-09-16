/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * Copyright (C) 2025, Ideas On Board Oy
 *
 * Helper class to manage conversions between floating point types and quantized
 * storage and representation of those values.
 */

#pragma once

#include <sstream>
#include <type_traits>

#include <libcamera/base/utils.h>

namespace libcamera {

namespace ipa {

template<typename Traits>
struct Quantized {
	using TraitsType = Traits;
	using QuantizedType = typename Traits::QuantizedType;
	using FloatingType = typename Traits::FloatingType;

	static_assert(std::is_integral_v<QuantizedType>,
		      "Quantized: QuantizedType must be integral");
	static_assert(std::is_floating_point_v<FloatingType>,
		      "Quantized: FloatingType must be floating");

	constexpr Quantized()
		: Quantized(FloatingType{}) {}

	constexpr Quantized(FloatingType x)
		: Quantized(Traits::fromFloat(x))
	{
	}

	constexpr Quantized(QuantizedType x)
		: quantized_(x), value_(Traits::toFloat(x))
	{
	}

	constexpr Quantized &operator=(float x)
	{
		*this = Quantized(x);
		return *this;
	}

	constexpr Quantized &operator=(QuantizedType x)
	{
		*this = Quantized(x);
		return *this;
	}

	constexpr float value() const { return value_; }
	constexpr QuantizedType quantized() const { return quantized_; }

	constexpr bool operator==(const Quantized &other) const
	{
		return quantized_ == other.quantized_;
	}

	constexpr bool operator!=(const Quantized &other) const
	{
		return !(*this == other);
	}

	friend std::ostream &operator<<(std::ostream &out,
					const Quantized<Traits> &q)
	{
		out << "[" << utils::hex(q.quantized())
		    << ":" << q.value() << "]";

		return out;
	}

private:
	QuantizedType quantized_;
	FloatingType value_;
};

} /* namespace ipa */

} /* namespace libcamera */
