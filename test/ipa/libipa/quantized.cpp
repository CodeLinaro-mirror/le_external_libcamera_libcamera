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

class BrightnessHueQuantizer : public Quantizer<int8_t>
{
public:
	BrightnessHueQuantizer(float val) { set(val); }
	BrightnessHueQuantizer(int8_t val) { set(val); }

	int8_t fromFloat(float v) const override
	{
		int quantized = std::lround(v * 128.0f);
		return static_cast<int8_t>(std::clamp<int>(quantized, -128, 127));
	}

	float toFloat(int8_t v) const override
	{
		return static_cast<float>(v) / 128.0f;
	}
};

class ContrastSaturationQuantizer : public Quantizer<uint8_t>
{
public:
	ContrastSaturationQuantizer(float val) { set(val); }
	ContrastSaturationQuantizer(uint8_t val) { set(val); }

	uint8_t fromFloat(float v) const override
	{
		int quantized = std::lround(v * 128.0f);
		return static_cast<uint8_t>(std::clamp<int>(quantized, 0, 255));
	}

	float toFloat(uint8_t v) const override
	{
		return static_cast<float>(v) / 128.0f;
	}
};

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
		// BrightnessQ bad1(15); // overloaded ‘BrightnessHueQuantizer(int)’ is ambiguous
		// BrightnessQ bad2(0xff); // overloaded ‘BrightnessHueQuantizer(int)’ is ambiguous
		// ContrastQ bad3(0x33); // overloaded ‘ContrastSaturationQuantizer(int)’ is ambiguous
		// ContrastQ bad4(55U); // overloaded ‘ContrastSaturationQuantizer(unsigned int)’ is ambiguous

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

		/* Test assignment / storage in generic type */
		{
			ContrastQ c(1.5f);
			Quantized<uint8_t> q;

			q = c;
			if (q.value() != c.value())
				return TestFail;

			if (q.quantized() != c.quantized())
				return TestFail;
		}

		std::cout << "Quantised tests passed successfully." << std::endl;

		return TestPass;
	}
};

TEST_REGISTER(QuantizedTest)
