/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (C) 2025, Ideas on Board
 *
 * Dual Type and Quantizer tests
 */

#include "../src/ipa/libipa/quantized.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <map>
#include <stdint.h>

#include "test.h"

using namespace std;
using namespace libcamera;
using namespace ipa;

struct BrightnessHueTraits {
	using quantized_type = int8_t;
	static constexpr quantized_type fromFloat(float v)
	{
		int quantized = std::lround(v * 128.0f);
		return static_cast<int8_t>(std::clamp<int>(quantized, -128, 127));
	}
	static float toFloat(quantized_type v)
	{
		return static_cast<float>(v) / 128.0f;
	}
};

using BrightnessHueQuantizer = Quantized<BrightnessHueTraits>;

struct ContrastSaturationTraits {
	using quantized_type = uint8_t;
	static constexpr quantized_type fromFloat(float v)
	{
		int quantized = std::lround(v * 128.0f);
		return static_cast<uint8_t>(std::clamp<int>(quantized, 0, 255));
	}
	static float toFloat(quantized_type v)
	{
		return static_cast<float>(v) / 128.0f;
	}
};

using ContrastSaturationQuantizer = Quantized<ContrastSaturationTraits>;

using BrightnessQ = BrightnessHueQuantizer;
using HueQ = BrightnessHueQuantizer;
using ContrastQ = ContrastSaturationQuantizer;
using SaturationQ = ContrastSaturationQuantizer;

class QuantizedTest : public Test
{
protected:
	int run()
	{
		/* Test construction from float */
		{
			BrightnessQ b(0.5f);
			if (b.quantized() != 64 || std::abs(b.value() - 0.5f) > 0.01f)
				return TestFail;
		}

		/* Test construction from T */
		{
			ContrastQ c(uint8_t(128));
			if (c.quantized() != 128 || std::abs(c.value() - 1.0f) > 0.01f)
				return TestFail;
		}

		/*
		 * Test construction from non-float/non-T type (Expected Fail)
		 * These should be a compile-time error if uncommented:
		 */
		// BrightnessQ bad1(15); // overloaded ‘Quantized(int)’ is ambiguous
		// BrightnessQ bad2(0xff); // overloaded ‘Quantized(int)’ is ambiguous
		// ContrastQ bad3(0x33); // overloaded ‘Quantized(int)’ is ambiguous
		// ContrastQ bad4(55U); // overloaded ‘Quantized(unsigned int)’ is ambiguous

		/* Test equality */
		{
			BrightnessQ b1(0.5f), b2((int8_t)64);
			if (!(b1 == b2))
				return TestFail;
		}

		/* Test inequality */
		{
			BrightnessQ b1(0.5f), b2(-0.5f);
			if (b1 == b2)
				return TestFail;
		}

		/* Test copying */
		{
			BrightnessQ b1(0.25f);
			BrightnessQ b2 = b1;
			if (!(b1 == b2))
				return TestFail;
		}

		/* Test moving */
		{
			BrightnessQ b1(0.25f);
			BrightnessQ b2 = std::move(b1); // Allow move semantics
			if (b2.value() != 0.25f)
				return TestFail;
		}

		/* Test assignment */
		{
			ContrastQ c1(1.5f);
			ContrastQ c2(0.0f);
			c2 = c1;
			if (!(c1 == c2))
				return TestFail;
		}

		std::cout << "Quantised tests passed successfully." << std::endl;

		return TestPass;
	}
};

TEST_REGISTER(QuantizedTest)
