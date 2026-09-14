/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (C) 2024, Ideas on Board Oy
 *
 * Matrix tests
 */

#include "libcamera/internal/matrix.h"

#include <cmath>
#include <iostream>

#include "test.h"

using namespace libcamera;

#define ASSERT_EQ(a, b)                                                   \
	if ((a) != (b)) {                                                 \
		std::cout << #a " != " #b << " (line " << __LINE__ << ")" \
			  << std::endl;                                   \
		return TestFail;                                          \
	}

class MatrixTest : public Test
{
protected:
	int run()
	{
		Matrix<double, 3, 3> m1;

		ASSERT_EQ(m1[0][0], 0.0);
		ASSERT_EQ(m1[0][1], 0.0);

		constexpr Matrix<float, 2, 2> m2 = Matrix<float, 2, 2>().identity();
		ASSERT_EQ(m2[0][0], 1.0);
		ASSERT_EQ(m2[0][1], 0.0);
		ASSERT_EQ(m2[1][0], 0.0);
		ASSERT_EQ(m2[1][1], 1.0);

		Matrix<float, 2, 2> m3{ { 2.0, 0.0, 0.0, 2.0 } };
		Matrix<float, 2, 2> m4 = m3.inverse();

		Matrix<float, 2, 2> m5 = m3 * m4;
		ASSERT_EQ(m5[0][0], 1.0);
		ASSERT_EQ(m5[0][1], 0.0);
		ASSERT_EQ(m5[1][0], 0.0);
		ASSERT_EQ(m5[1][1], 1.0);

		Matrix<float, 2, 3> m6({ 1.0, 2.0, 3.0, 4.0, 5.0, 6.0 });
		Matrix<float, 3, 2> m7 = m6.transpose();
		ASSERT_EQ(m7[0][0], 1.0);
		ASSERT_EQ(m7[0][1], 4.0);
		ASSERT_EQ(m7[1][0], 2.0);
		ASSERT_EQ(m7[1][1], 5.0);
		ASSERT_EQ(m7[2][0], 3.0);
		ASSERT_EQ(m7[2][1], 6.0);

		return TestPass;
	}
};

TEST_REGISTER(MatrixTest)
