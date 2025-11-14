/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (C) 2024, Paul Elder <paul.elder@ideasonboard.com>
 *
 * Fixed / Floating point utility tests
 */

#include "../src/ipa/libipa/fixedpoint.h"

#include <cmath>
#include <iostream>
#include <map>
#include <stdint.h>

#include <libcamera/base/utils.h>

#include "test.h"

using namespace std;
using namespace libcamera;
using namespace ipa;

/* Q4_7(-8 .. 7.99219)  Min: [0x0400:-8] -- Max: [0x03ff:7.99219] Step:0.0078125 */
using Q4_7 = Quantized<FixedPointQTraits<4, 7, int16_t>>;

class FixedPointUtilsTest : public Test
{
protected:
	template<typename Q>
	int quantizedCheck(float input, typename Q::QuantizedType expected, float value)
	{
		Q q(input);
		using T = typename Q::QuantizedType;

		cerr << "  Checking " << input << " == " << q.toString() << std::endl;

		T quantized = q.quantized();
		if (quantized != expected) {
			cerr << "    ** Q Expected " << input
			     << " to quantize to " << utils::hex(expected)
			     << ", got " << utils::hex(quantized)
			     << " - (" << q.toString() << ")"
			     << std::endl;
			return 1;
		}

		if ((std::abs(q.value() - value)) > 0.0001f) {
			cerr << "    ** V Expected " << input
			     << " to quantize to " << value
			     << ", got " << q.value()
			     << " - (" << q.toString() << ")"
			     << std::endl;
			return 1;
		}

		return 0;
	}

	template<typename Q>
	int introduce(std::string type)
	{
		using T = typename Q::QuantizedType;

		std::cerr << std::endl;

		cerr << type << "(" << Q::TraitsType::min << " .. " << Q::TraitsType::max << ") "
		     << " Min: " << Q(Q::TraitsType::min).toString()
		     << " -- Max: " << Q(Q::TraitsType::max).toString()
		     << " Step:" << Q(T(1)).value()
		     << std::endl;

		if (Q::TraitsType::min > Q::TraitsType::max) {
			cerr << "    *** " << type
			     << " Min (" << Q::TraitsType::min
			     << ") must be less than max ("
			     << Q::TraitsType::max << ")" << std::endl;
			return 1;
		}

		return 0;
	}

	int testFixedPointQuantizers()
	{
		unsigned int fails = 0;

		/* clang-format off */

		/* Q1_7(-1 .. 0.992188)  Min: [0x80:-1] -- Max: [0x7f:0.992188] Step:0.0078125*/
		fails += introduce<Q1_7>("Q1_7");
		fails += quantizedCheck<Q1_7>(-1.000f, 0b1'0000000, -1.0f);		/* Min */
		fails += quantizedCheck<Q1_7>(-0.992f, 0b1'0000001, -0.992188f);	/* Min + 1 step */
		fails += quantizedCheck<Q1_7>(-0.006f, 0b1'1111111, -0.0078125f);	/* -1 step */
		fails += quantizedCheck<Q1_7>( 0.000f, 0b0'0000000,  0.0f);		/* Zero */
		fails += quantizedCheck<Q1_7>( 0.008f, 0b0'0000001,  0.0078125f);	/* +1 step */
		fails += quantizedCheck<Q1_7>( 0.992f, 0b0'1111111,  0.992188f);	/* Max */

		/* UQ1_7(0 .. 1.99219)  Min: [0x00:0] -- Max: [0xff:1.99219] Step:0.0078125 */
		fails += introduce<UQ1_7>("UQ1_7");
		fails += quantizedCheck<UQ1_7>(0.0f,   0b0'0000000, 0.0f);		/* Min / Zero */
		fails += quantizedCheck<UQ1_7>(1.0f,   0b1'0000000, 1.0f);		/* Mid */
		fails += quantizedCheck<UQ1_7>(1.992f, 0b1'1111111, 1.99219f);		/* Max */

		/* Q4_7(-8 .. 7.99219)  Min: [0x0400:-8] -- Max: [0x03ff:7.99219] Step:0.0078125 */
		introduce<Q4_7>("Q4_7");
		fails += quantizedCheck<Q4_7>(-8.0f,   0b1000'0000000, -8.0f);		/* Min */
		fails += quantizedCheck<Q4_7>(-0.008f, 0b1111'1111111, -0.0078125);	/* -1 step */
		fails += quantizedCheck<Q4_7>( 0.0f,   0b0000'0000000,  0.0f);		/* Zero */
		fails += quantizedCheck<Q4_7>( 0.008f, 0b0000'0000001,  0.0078125f);	/* +1 step */
		fails += quantizedCheck<Q4_7>( 7.992f, 0b0111'1111111,  7.99219f);	/* Max */

		/* Retain additional tests from original testFixedPoint() */
		fails += quantizedCheck<Q4_7>( 0.2f, 0b0000'0011010,  0.203125f);	/* 0x01a */
		fails += quantizedCheck<Q4_7>(-0.2f, 0b1111'1100110, -0.203125f);	/* 0x7e6 */
		fails += quantizedCheck<Q4_7>(-0.8f, 0b1111'0011010, -0.796875f);	/* 0x79a */
		fails += quantizedCheck<Q4_7>(-0.4f, 0b1111'1001101, -0.398438f);	/* 0x7cd */
		fails += quantizedCheck<Q4_7>(-1.4f, 0b1110'1001101, -1.39844f);	/* 0x74d */

		/* UQ4_8(0 .. 15.9961)  Min: [0x0000:0] -- Max: [0x0fff:15.9961] Step:0.00390625 */
		introduce<UQ4_8>("UQ4_8");
		fails += quantizedCheck<UQ4_8>( 0.0f, 0b0000'00000000,  0.00f);
		fails += quantizedCheck<UQ4_8>(16.0f, 0b1111'11111111, 15.9961f);

		/* Q5_4(-16 .. 15.9375)  Min: [0x0100:-16] -- Max: [0x00ff:15.9375] Step:0.0625 */
		introduce<Q5_4>("Q5_4");
		fails += quantizedCheck<Q5_4>(-16.00f, 0b10000'0000, -16.00f);
		fails += quantizedCheck<Q5_4>( 15.94f, 0b01111'1111,  15.9375f);

		/* UQ5_8(0 .. 31.9961)  Min: [0x0000:0] -- Max: [0x1fff:31.9961] Step:0.00390625 */
		introduce<UQ5_8>("UQ5_8");
		fails += quantizedCheck<UQ5_8>( 0.00f, 0b00000'00000000,  0.00f);
		fails += quantizedCheck<UQ5_8>(32.00f, 0b11111'11111111, 31.9961f);

		/* Q12.4(-2048 .. 2047.94)  Min: [0x8000:-2048] -- Max: [0x7fff:2047.94] Step:0.0625 */
		introduce<Q12_4>("Q12_4");
		fails += quantizedCheck<Q12_4>(0.0f, 0b000000000000'0000, 0.0f);
		fails += quantizedCheck<Q12_4>(7.5f, 0b000000000111'1000, 7.5f);

		/* UQ12_4(0 .. 4095.94)  Min: [0x0000:0] -- Max: [0xffff:4095.94] Step:0.0625 */
		introduce<UQ12_4>("UQ12_4");
		fails += quantizedCheck<UQ12_4>(0.0f, 0b000000000000'0000, 0.0f);
		fails += quantizedCheck<UQ12_4>(7.5f, 0b000000000111'1000, 7.5f);

		/* Validate that exceeding limits clamps to type range */
		cerr << std::endl << "Range validation:" << std::endl;
		fails += quantizedCheck<Q1_7>(-100.0f, 0b1'0000000, -1.0f);
		fails += quantizedCheck<Q1_7>(+100.0f, 0b0'1111111, 0.992188f);
		fails += quantizedCheck<UQ1_7>(-100.0f, 0b0'0000000, 0.0f);
		fails += quantizedCheck<UQ1_7>(+100.0f, 0b1'1111111, 1.99219f);

		/* clang-format on */

		std::cerr << std::endl;

		if (fails > 0) {
			cerr << "Fixed point quantizer tests failed: "
			     << std::dec << fails << " failures." << std::endl;
			return TestFail;
		}

		return TestPass;
	}

	int testScaledFixedPointQuantizers()
	{
		unsigned int fails = 0;
		cerr << std::endl
		     << "Scaled Fixed-Point Quantizer tests:" << std::endl;

		using HueQ = Quantized<ScaledFixedPointQTraits<Q1_7::TraitsType, 90>>;

		/* HueQ (Scaled Q1_7 * 90) (-90 .. 89.2969)  Min: [0x80:-90] -- Max: [0x7f:89.2969] Step:0.703125 */
		fails += introduce<HueQ>("HueQ (Scaled Q1_7 * 90) ");

		/* clang-format off */

		fails += quantizedCheck<HueQ>(-90.0f, -128, -90.0f);	/* Min */
		fails += quantizedCheck<HueQ>(-45.0f,  -64, -45.0f);	/* Mid negative */
		fails += quantizedCheck<HueQ>(  0.0f,    0,   0.0f);	/* Zero */
		fails += quantizedCheck<HueQ>( 45.0f,   64,  45.0f);	/* Mid positive */
		fails += quantizedCheck<HueQ>( 90.0f,  127,  89.2969f);	/* Max */

		fails += quantizedCheck<HueQ>(-99.0f, -128, -90.0f);	/* Clamp low */
		fails += quantizedCheck<HueQ>( 99.0f,  127,  89.2969f);	/* Clamp high */

		/* clang-format on */

		return fails ? TestFail : TestPass;
	}

	int run()
	{
		unsigned int fails = 0;

		if (testFixedPointQuantizers() != TestPass)
			fails++;

		if (testScaledFixedPointQuantizers() != TestPass)
			fails++;

		return fails ? TestFail : TestPass;
	}
};

TEST_REGISTER(FixedPointUtilsTest)
