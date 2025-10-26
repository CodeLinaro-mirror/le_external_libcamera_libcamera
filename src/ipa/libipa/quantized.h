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

namespace libcamera {

namespace ipa {

template<typename Q>
class Quantizer;

template<typename T>
struct Quantized {
	static_assert(std::is_arithmetic_v<T>, "Quantized: T must be an arithmetic type");

	float value() const noexcept { return value_; }
	T quantized() const noexcept { return quantized_; }

	std::string toString() const
	{
		using UT = std::make_unsigned_t<T>;
		std::ostringstream oss;

		oss << "Q:0x" << std::hex << std::uppercase << std::setw(sizeof(T) * 2)
		    << std::setfill('0');

		if constexpr (std::is_unsigned_v<T>) {
			oss << static_cast<unsigned>(quantized_);
		} else {
			oss << static_cast<unsigned>(static_cast<UT>(quantized_));
		}

		oss << " F:" << value_;

		return oss.str();
	}

	bool operator==(const Quantized<T> &other) const noexcept
	{
		return quantized_ == other.quantized_ && value_ == other.value_;
	}

	bool operator!=(const Quantized<T> &other) const noexcept
	{
		return !(*this == other);
	}

protected:
	T quantized_{};
	float value_{};
};

template<typename T>
class Quantizer : public Quantized<T>
{
public:
	using qType = T;

	virtual ~Quantizer() = default;

	virtual T fromFloat(float val) const = 0;
	virtual float toFloat(T val) const = 0;

	void set(float val)
	{
		this->quantized_ = fromFloat(val);
		this->value_ = toFloat(this->quantized_);
	}

	void set(T val)
	{
		this->quantized_ = val;
		this->value_ = toFloat(val);
	}
};

} /* namespace ipa */

} /* namespace libcamera */
