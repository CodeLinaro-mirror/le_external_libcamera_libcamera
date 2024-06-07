/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * Copyright (C) 2019, Raspberry Pi Ltd
 * Copyright (C) 2024, Paul Elder <paul.elder@ideasonboard.com>
 *
 * Vector and related operations
 */
#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <sstream>

#include <libcamera/base/log.h>
#include <libcamera/base/span.h>

#include "libcamera/internal/yaml_parser.h"

namespace libcamera {

LOG_DECLARE_CATEGORY(Vector)

namespace ipa {

#ifndef __DOXYGEN__
template<typename T, unsigned int R,
	 std::enable_if_t<std::is_arithmetic_v<T> && R >= 2> * = nullptr>
#else
template<typename T, unsigned int R>
#endif /* __DOXYGEN__ */
class Vector
{
public:
	Vector() = default;

	Vector(const std::array<T, R> &data)
	{
		ASSERT(data.size() == R);

		for (unsigned int i = 0; i < R; i++)
			data_[i] = data[i];
	}

	~Vector() = default;

	int readYaml(const libcamera::YamlObject &yaml)
	{
		if (yaml.size() != R) {
			LOG(Vector, Error)
				<< "Wrong number of values in vector: expected "
				<< R << ", got " << yaml.size();
			return -EINVAL;
		}

		unsigned int i = 0;
		for (const auto &x : yaml.asList()) {
			auto value = x.get<T>();
			if (!value) {
				LOG(Vector, Error) << "Failed to read vector value";
				return -EINVAL;
			}

			data_[i++] = *value;
		}

		return 0;
	}

	const std::string toString() const
	{
		std::stringstream out;

		out << "Vector { ";
		for (unsigned int i = 0; i < R; i++) {
			out << (*this)[i];
			out << ((i + 1 < R) ? ", " : " ");
		}
		out << " }";

		return out.str();
	}

	const T operator[](size_t i) const
	{
		return data_[i];
	}

	T &operator[](size_t i)
	{
		return data_[i];
	}

	const T x() const
	{
		return data_[0];
	}

	const T y() const
	{
		return data_[1];
	}

	constexpr Vector<T, R> operator-() const
	{
		Vector<T, R> ret;
		for (unsigned int i = 0; i < R; i++)
			ret[i] = -data_[i];
		return ret;
	}

	constexpr Vector<T, R> operator-(const Vector<T, R> &other) const
	{
		Vector<T, R> ret;
		for (unsigned int i = 0; i < R; i++)
			ret[i] = data_[i] - other[i];
		return ret;
	}

	constexpr Vector<T, R> operator+(const Vector<T, R> &other) const
	{
		Vector<T, R> ret;
		for (unsigned int i = 0; i < R; i++)
			ret[i] = data_[i] + other[i];
		return ret;
	}

	constexpr T operator*(const Vector<T, R> &other) const
	{
		T ret = 0;
		for (unsigned int i = 0; i < R; i++)
			ret += data_[i] * other[i];
		return ret;
	}

	constexpr Vector<T, R> operator*(T factor) const
	{
		Vector<T, R> ret;
		for (unsigned int i = 0; i < R; i++)
			ret[i] = data_[i] * factor;
		return ret;
	}

	constexpr Vector<T, R> operator/(T factor) const
	{
		Vector<T, R> ret;
		for (unsigned int i = 0; i < R; i++)
			ret[i] = data_[i] / factor;
		return ret;
	}

	constexpr T len2() const
	{
		T ret = 0;
		for (unsigned int i = 0; i < R; i++)
			ret += data_[i] * data_[i];
		return ret;
	}

	constexpr double len() const
	{
		return std::sqrt(len2());
	}

private:
	std::array<T, R> data_;
};

#ifndef __DOXYGEN__
template<typename T, unsigned int R,
	 std::enable_if_t<std::is_arithmetic_v<T>> * = nullptr>
#endif /* __DOXYGEN__ */
bool operator==(const Vector<T, R> &lhs, const Vector<T, R> &rhs)
{
	for (unsigned int i = 0; i < R; i++)
		if (lhs[i] != rhs[i])
			return false;

	return true;
}

#ifndef __DOXYGEN__
template<typename T, unsigned int R,
	 std::enable_if_t<std::is_arithmetic_v<T>> * = nullptr>
#endif /* __DOXYGEN__ */
bool operator!=(const Vector<T, R> &lhs, const Vector<T, R> &rhs)
{
	for (unsigned int i = 0; i < R; i++)
		if (lhs[i] != rhs[i])
			return true;

	return false;
}

} /* namespace ipa */

#ifndef __DOXYGEN__
template<typename T, unsigned int R>
std::ostream &operator<<(std::ostream &out, const ipa::Vector<T, R> &v)
{
	out << v.toString();
	return out;
}
#endif /* __DOXYGEN__ */

} /* namespace libcamera */
