/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * Copyright (C) 2025, Ideas On Board Oy
 *
 * Helper class to manage conversions between floating point types and quantized
 * storage and representation of those values.
 */

#pragma once

#include <iomanip>
#include <sstream>
#include <stdint.h>
#include <type_traits>

#include <libcamera/base/utils.h>

namespace libcamera {

namespace ipa {

template<typename Traits>
struct Quantized {
	using TraitsType = Traits;
	using QuantizedType = typename Traits::QuantizedType;
	static_assert(std::is_arithmetic_v<QuantizedType>,
		      "Quantized: QuantizedType must be arithmetic");

	Quantized()
		: Quantized(0.0f) {}
	Quantized(float x) { *this = x; }
	Quantized(QuantizedType x) { *this = x; }

	Quantized &operator=(float x)
	{
		quantized_ = Traits::fromFloat(x);
		value_ = Traits::toFloat(quantized_);
		return *this;
	}

	Quantized &operator=(QuantizedType x)
	{
		value_ = Traits::toFloat(x);
		quantized_ = x;
		return *this;
	}

	float value() const noexcept { return value_; }
	QuantizedType quantized() const noexcept { return quantized_; }

	std::string toString() const
	{
		std::ostringstream oss;

		oss << "[" << utils::hex(quantized_)
		    << ":" << value_ << "]";

		return oss.str();
	}

	bool operator==(const Quantized &other) const noexcept
	{
		return quantized_ == other.quantized_;
	}

	bool operator!=(const Quantized &other) const noexcept
	{
		return !(*this == other);
	}

private:
	QuantizedType quantized_;
	float value_;
};

} /* namespace ipa */

} /* namespace libcamera */
