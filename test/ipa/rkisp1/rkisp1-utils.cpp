/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (C) 2024, Paul Elder <paul.elder@ideasonboard.com>
 *
 * Miscellaneous utility tests
 */

#include <cmath>
#include <iostream>
#include <stdint.h>
#include <tuple>

#include "../src/ipa/rkisp1/utils.h"

#include "test.h"

using namespace std;
using namespace libcamera;
using namespace ipa::rkisp1;

class RkISP1UtilsTest : public Test
{
protected:
	/* R for real, I for integer */
	template<unsigned int IntPrec, unsigned FracPrec, typename I, typename R>
	int testFixedToFloat(I input, R expected, R *output = nullptr)
	{
		R out = utils::fixedToFloatingPoint<IntPrec, FracPrec, R>(input);
		if (output)
			*output = out;
		R prec = 1.0 / (1 << FracPrec);
		if (std::abs(out - expected) > prec) {
			cerr << "Reverse conversion expected " << input
			     << " to convert to " << expected
			     << ", got " << out << std::endl;
			return TestFail;
		}

		return TestPass;
	}

	/* R for real, I for integer */
	template<unsigned int IntPrec, unsigned FracPrec, typename R, typename I>
	int testFloatToFixed(R input, I expected, I *output = nullptr)
	{
		I out = utils::floatingToFixedPoint<IntPrec, FracPrec, I>(input);
		if (output)
			*output = out;
		if (out != expected) {
			cerr << "Expected " << input << " to convert to "
			     << expected << ", got " << out << std::endl;
			return TestFail;
		}

		return TestPass;
	}

	/* R for real, I for integer */
	template<unsigned int IntPrec, unsigned FracPrec, typename R, typename I>
	int testFullConversion(R input, I expected)
	{
		I outInt;
		R outReal;
		int status;

		status = testFloatToFixed<IntPrec, FracPrec, R, I>(input, expected, &outInt);
		if (status != TestPass)
			return status;

		status = testFixedToFloat<IntPrec, FracPrec, I, R>(outInt, input, &outReal);
		if (status != TestPass)
			return status;

		return TestPass;
	}


	int testFixedPoint()
	{
		/*
		 * The second 7.992 test is to test that unused bits don't
		 * affect the result.
		 *
		 * Third parameter is for testing forward, fourth parameter is
		 * for testing reverse.
		 */
		static const std::tuple<double, uint16_t, bool, bool> testCases[] = {
			{ 7.992, 0x3ff,  true, true },
			{ 7.992, 0xbff, false, true },
			{   0.2, 0x01a,  true, true },
			{  -0.2, 0x7e6,  true, true },
			{  -0.8, 0x79a,  true, true },
			{  -0.4, 0x7cd,  true, true },
			{  -1.4, 0x74d,  true, true },
			{    -8, 0x400,  true, true },
			{     0,     0,  true, true },
		};

		int ret;
		for (const auto &testCase : testCases) {
			double floating;
			uint16_t fixed;
			bool forward, backward;
			std::tie(floating, fixed, forward, backward) = testCase;
			if (forward && backward)
				ret = testFullConversion<4, 7, double, uint16_t>(floating, fixed);
			else if (forward)
				ret = testFloatToFixed<4, 7, double, uint16_t>(floating, fixed);
			else if (backward)
				ret = testFixedToFloat<4, 7, uint16_t, double>(fixed, floating);

			if (ret != TestPass)
				return ret;
		}

		return TestPass;
	}

	int run()
	{
		/* fixed point conversion test */
		if (testFixedPoint() != TestPass)
			return TestFail;

		return TestPass;
	}
};

TEST_REGISTER(RkISP1UtilsTest)
