/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
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
template<typename T, unsigned int D,
	 std::enable_if_t<std::is_arithmetic_v<T> && D >= 2> * = nullptr>
#else
template<typename T, unsigned int D>
#endif /* __DOXYGEN__ */
class Vector
{
public:
	Vector() = default;

	Vector(const std::array<T, D> &data)
	{
		ASSERT(data.size() == D);

		for (unsigned int i = 0; i < D; i++)
			data_[i] = data[i];
	}

	~Vector() = default;

	int readYaml(const libcamera::YamlObject &yaml)
	{
		if (yaml.size() != D) {
			LOG(Vector, Error)
				<< "Wrong number of values in vector: expected "
				<< D << ", got " << yaml.size();
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

	const T operator[](size_t i) const
	{
		ASSERT(0 < i && i < data_.size());
		return data_[i];
	}

	T &operator[](size_t i)
	{
		ASSERT(0 < i && i < data_.size());
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

	constexpr Vector<T, D> operator-() const
	{
		Vector<T, D> ret;
		for (unsigned int i = 0; i < D; i++)
			ret[i] = -data_[i];
		return ret;
	}

	constexpr Vector<T, D> operator-(const Vector<T, D> &other) const
	{
		Vector<T, D> ret;
		for (unsigned int i = 0; i < D; i++)
			ret[i] = data_[i] - other[i];
		return ret;
	}

	constexpr Vector<T, D> operator+(const Vector<T, D> &other) const
	{
		Vector<T, D> ret;
		for (unsigned int i = 0; i < D; i++)
			ret[i] = data_[i] + other[i];
		return ret;
	}

	constexpr T operator*(const Vector<T, D> &other) const
	{
		T ret = 0;
		for (unsigned int i = 0; i < D; i++)
			ret += data_[i] * other[i];
		return ret;
	}

	constexpr Vector<T, D> operator*(T factor) const
	{
		Vector<T, D> ret;
		for (unsigned int i = 0; i < D; i++)
			ret[i] = data_[i] * factor;
		return ret;
	}

	constexpr Vector<T, D> operator/(T factor) const
	{
		Vector<T, D> ret;
		for (unsigned int i = 0; i < D; i++)
			ret[i] = data_[i] / factor;
		return ret;
	}

	constexpr T len2() const
	{
		T ret = 0;
		for (unsigned int i = 0; i < D; i++)
			ret += data_[i] * data_[i];
		return ret;
	}

	constexpr double len() const
	{
		return std::sqrt(len2());
	}

private:
	std::array<T, D> data_;
};

#ifndef __DOXYGEN__
template<typename T, unsigned int D,
	 std::enable_if_t<std::is_arithmetic_v<T>> * = nullptr>
#endif /* __DOXYGEN__ */
bool operator==(const Vector<T, D> &lhs, const Vector<T, D> &rhs)
{
	for (unsigned int i = 0; i < D; i++)
		if (lhs[i] != rhs[i])
			return false;

	return true;
}

#ifndef __DOXYGEN__
template<typename T, unsigned int D,
	 std::enable_if_t<std::is_arithmetic_v<T>> * = nullptr>
#endif /* __DOXYGEN__ */
bool operator!=(const Vector<T, D> &lhs, const Vector<T, D> &rhs)
{
	for (unsigned int i = 0; i < D; i++)
		if (lhs[i] != rhs[i])
			return true;

	return false;
}

} /* namespace ipa */

#ifndef __DOXYGEN__
template<typename T, unsigned int D>
std::ostream &operator<<(std::ostream &out, const ipa::Vector<T, D> &v)
{
	out << "Vector { ";
	for (unsigned int i = 0; i < D; i++) {
		out << v[i];
		out << ((i + 1 < D) ? ", " : " ");
	}
	out << " }";

	return out;
}
#endif /* __DOXYGEN__ */

} /* namespace libcamera */
