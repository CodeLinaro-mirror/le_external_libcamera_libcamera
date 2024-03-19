/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * Copyright (C) 2024, Paul Elder <paul.elder@ideasonboard.com>
 *
 * Matrix and related operations
 */
#pragma once

#include <algorithm>
#include <cmath>
#include <sstream>
#include <vector>

#include <libcamera/base/log.h>
#include <libcamera/base/span.h>

#include "libcamera/internal/yaml_parser.h"

namespace libcamera {

LOG_DECLARE_CATEGORY(Matrix)

namespace ipa {

#ifndef __DOXYGEN__
template<typename T, unsigned int R, unsigned int C,
	 std::enable_if_t<std::is_arithmetic_v<T>> * = nullptr>
#else
template<typename T, unsigned int R, unsigned int C>
#endif /* __DOXYGEN__ */
class Matrix
{
public:
	Matrix()
		: data_(R * C, static_cast<T>(false))
	{
		for (size_t i = 0; i < std::min(R, C); i++)
			(*this)[i][i] = static_cast<T>(true);
	}

	Matrix(const std::vector<T> &data)
	{
		ASSERT(data.size() == R * C);

		data_.clear();
		for (const T &x : data)
			data_.push_back(x);
	}

	~Matrix() = default;

	int readYaml(const libcamera::YamlObject &yaml)
	{
		if (yaml.size() != R * C) {
			LOG(Matrix, Error)
				<< "Wrong number of values in matrix: expected "
				<< R * C << ", got " << yaml.size();
			return -EINVAL;
		}

		unsigned int i = 0;
		for (const auto &x : yaml.asList()) {
			auto value = x.get<T>();
			if (!value) {
				LOG(Matrix, Error) << "Failed to read matrix value";
				return -EINVAL;
			}

			data_[i++] = *value;
		}

		return 0;
	}

	const std::string toString() const
	{
		std::stringstream out;

		out << "Matrix { ";
		for (unsigned int i = 0; i < R; i++) {
			out << "[ ";
			for (unsigned int j = 0; j < C; j++) {
				out << (*this)[i][j];
				out << ((j + 1 < C) ? ", " : " ");
			}
			out << ((i + 1 < R) ? "], " : "]");
		}
		out << " }";

		return out.str();
	}

	Span<const T, C> operator[](size_t i) const
	{
		return Span<const T, C>{ &data_.data()[i * C], C };
	}

	Span<T, C> operator[](size_t i)
	{
		return Span<T, C>{ &data_.data()[i * C], C };
	}

private:
	std::vector<T> data_;
};

#ifndef __DOXYGEN__
template<typename T, typename U, unsigned int R, unsigned int C,
	 std::enable_if_t<std::is_arithmetic_v<T> && std::is_arithmetic_v<U>> * = nullptr>
#endif /* __DOXYGEN__ */
Matrix<U, R, C> operator*(T d, const Matrix<U, R, C> &m)
{
	Matrix<U, R, C> result;

	for (unsigned int i = 0; i < R; i++)
		for (unsigned int j = 0; j < C; j++)
			result[i][j] = d * m[i][j];

	return result;
}

#ifndef __DOXYGEN__
template<typename T,
	 unsigned int R1, unsigned int C1,
	 unsigned int R2, unsigned int C2,
	 std::enable_if_t<std::is_arithmetic_v<T> && C1 == R2> * = nullptr>
#endif /* __DOXYGEN__ */
Matrix<T, R1, C2> operator*(const Matrix<T, R1, C1> &m1, const Matrix<T, R2, C2> &m2)
{
	Matrix<T, R1, C2> result;

	for (unsigned int i = 0; i < R1; i++) {
		for (unsigned int j = 0; j < C2; j++) {
			T sum = 0;

			for (unsigned int k = 0; k < C1; k++)
				sum += m1[i][k] * m2[k][j];

			result[i][j] = sum;
		}
	}

	return result;
}

#ifndef __DOXYGEN__
template<typename T, unsigned int R, unsigned int C,
	 std::enable_if_t<std::is_arithmetic_v<T>> * = nullptr>
#endif /* __DOXYGEN__ */
Matrix<T, R, C> operator+(const Matrix<T, R, C> &m1, const Matrix<T, R, C> &m2)
{
	Matrix<T, R, C> result;

	for (unsigned int i = 0; i < R; i++)
		for (unsigned int j = 0; j < C; j++)
			result[i][j] = m1[i][j] + m2[i][j];

	return result;
}

} /* namespace ipa */

#ifndef __DOXYGEN__
template<typename T, unsigned int R, unsigned int C>
std::ostream &operator<<(std::ostream &out, const ipa::Matrix<T, R, C> &m)
{
	out << m.toString();
	return out;
}
#endif /* __DOXYGEN__ */

} /* namespace libcamera */
