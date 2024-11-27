/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * Copyright (C) 2024 Ideas On Board Oy.
 *
 * BitDepth class to abstract bit shift operations
 */

#pragma once

template<unsigned int BitDepth>
class BitDepthValue
{
public:
	static_assert(BitDepth > 0, "Bit depth must be positive");

	BitDepthValue()
		: value_(0) {}

	BitDepthValue(int value)
		: value_(value) {}

	BitDepthValue(unsigned int value)
		: value_(value) {}

	template<unsigned int TargetBitDepth>
	BitDepthValue<TargetBitDepth> convert() const
	{
		static_assert(TargetBitDepth > 0, "Bit depth must be positive");

		unsigned int shift;

		if constexpr (BitDepth > TargetBitDepth) {
			shift = BitDepth - TargetBitDepth;
			return BitDepthValue<TargetBitDepth>(value_ >> shift);
		} else if constexpr (BitDepth < TargetBitDepth) {
			shift = TargetBitDepth - BitDepth;
			return BitDepthValue<TargetBitDepth>(value_ << shift);
		} else {
			return BitDepthValue<TargetBitDepth>(value_);
		}
	}

	unsigned int value() const
	{
		return value_;
	}

	template<unsigned int TargetBitDepth>
	operator BitDepthValue<TargetBitDepth>() const
	{
		return convert<TargetBitDepth>();
	}

	operator unsigned int() const
	{
		return value_;
	}

private:
	unsigned int value_;
};

inline BitDepthValue<8> operator"" _8bit(unsigned long long value)
{
	return BitDepthValue<8>(static_cast<unsigned int>(value));
}

inline BitDepthValue<10> operator"" _10bit(unsigned long long value)
{
	return BitDepthValue<10>(static_cast<unsigned int>(value));
}

inline BitDepthValue<12> operator"" _12bit(unsigned long long value)
{
	return BitDepthValue<12>(static_cast<unsigned int>(value));
}

inline BitDepthValue<14> operator"" _14bit(unsigned long long value)
{
	return BitDepthValue<14>(static_cast<unsigned int>(value));
}

inline BitDepthValue<16> operator"" _16bit(unsigned long long value)
{
	return BitDepthValue<16>(static_cast<unsigned int>(value));
}
