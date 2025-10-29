/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * Copyright (C) 2025, Ideas On Board.
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
	using quantized_type = typename Traits::quantized_type;
	static_assert(std::is_arithmetic_v<quantized_type>,
		      "Quantized: quantized_type must be arithmetic");

	/* Constructors */
	Quantized()
		: Quantized(0.0f) {}
	Quantized(float x) { *this = x; }
	Quantized(quantized_type x) { *this = x; }

	Quantized &operator=(float x)
	{
		quantized_ = Traits::fromFloat(x);
		value_ = Traits::toFloat(quantized_);
		return *this;
	}

	Quantized &operator=(quantized_type x)
	{
		value_ = Traits::toFloat(x);
		quantized_ = x;
		return *this;
	}

	float value() const noexcept { return value_; }
	quantized_type quantized() const noexcept { return quantized_; }

	std::string toString() const
	{
		std::ostringstream oss;

		oss << "Q:" << utils::hex(quantized_)
		    << " V:" << value_;

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
	quantized_type quantized_{};
	float value_{};
};

} /* namespace ipa */

} /* namespace libcamera */
