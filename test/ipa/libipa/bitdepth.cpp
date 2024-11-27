/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * Copyright (C) 2024 Ideas On Board Oy.
 *
 * BitDepth converter tests
 */

#include "../../../src/ipa/libipa/bitdepth.h"

#include <cmath>
#include <iostream>
#include <map>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include <libcamera/base/log.h>

#include "../../libtest/test.h"
using namespace std;

#define ASSERT_EQ(a, b)                    \
	if ((a) != (b)) {                  \
		printf(#a " != " #b "\n"); \
		return TestFail;           \
	}

class BitDepthConverterTest : public Test
{
protected:
	int run()
	{
		/* 10-bit to 16-bit conversion */
		BitDepthValue<10> value10bit = 64_10bit;
		BitDepthValue<16> value16bit = value10bit.convert<16>();
		ASSERT_EQ(value16bit.value(), 4096);

		/* Convert implicitly from another BitDepthValue */
		value10bit = BitDepthValue<8>(16);
		ASSERT_EQ(value10bit.value(), 64);
		value10bit = 1_8bit;
		ASSERT_EQ(value10bit.value(), 4);

		/* Read value explicity and implicitly */
		value10bit = BitDepthValue<10>(64);
		ASSERT_EQ(value10bit.value(), 64);
		ASSERT_EQ(value10bit, 64);

		/* 12-bit to 8-bit conversion */
		BitDepthValue<12> value12bit = 4096_12bit;
		BitDepthValue<8> value8bit = value12bit.convert<8>();
		ASSERT_EQ(value8bit.value(), 256);

		/* Explicit bit depth assignment and conversion */
		value16bit = 32768_16bit;
		value12bit = value16bit.convert<12>();
		ASSERT_EQ(value12bit.value(), 2048);

		/* Test hex conversion */
		value8bit = 0xFF_8bit;
		ASSERT_EQ(value8bit, 255);

		/* Test conversion to same bit depth makes no difference */
		value16bit = 255_16bit;
		value16bit = value16bit.convert<16>();
		ASSERT_EQ(value16bit, 255);

		/* Implicit bit depth assignment */
		value12bit = 10;
		ASSERT_EQ(value12bit.value(), 10);

		/* 8-bit to 12-bit conversion */
		value8bit = 128_8bit;
		value12bit = value8bit.convert<12>();
		ASSERT_EQ(value12bit.value(), 2048);

		/* 16-bit to 8-bit conversion */
		value16bit = 65535_16bit;
		value8bit = value16bit.convert<8>();
		ASSERT_EQ(value8bit.value(), 255);

		/* Implicit assignment with int */
		value8bit = 200;
		ASSERT_EQ(value8bit.value(), 200);

		/* 8-bit to 16-bit and back again */
		value8bit = 150_8bit;
		value16bit = value8bit.convert<16>();
		value8bit = value16bit.convert<8>();
		ASSERT_EQ(value8bit.value(), 150);

		/* 12-bit to 16-bit and back again */
		value12bit = 3000_12bit;
		value16bit = value12bit.convert<16>();
		ASSERT_EQ(value12bit, 3000);
		ASSERT_EQ(value16bit, 48000)
		value12bit = value16bit.convert<12>();
		ASSERT_EQ(value12bit.value(), 3000);

		/* Test negatives fail */
		//value12bit = BitDepthValue<-1>;

		return TestPass;
	}
};

TEST_REGISTER(BitDepthConverterTest)
