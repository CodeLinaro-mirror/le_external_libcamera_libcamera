/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * Copyright (C) 2019, Raspberry Pi Ltd
 *
 * Piecewise linear functions interface
 */
#pragma once

#include <functional>
#include <math.h>
#include <string>
#include <vector>

#include <libcamera/geometry.h>

#include "libcamera/internal/yaml_parser.h"

#include "vector.h"

namespace libcamera {

namespace ipa {

class Pwl
{
public:
	using PointF = Vector<double, 2>;

	enum class PerpType {
		None,
		Start,
		End,
		Vertex,
		Perpendicular,
	};

	struct Interval {
		Interval(double _start, double _end)
			: start(_start), end(_end) {}

		bool contains(double value)
		{
			return value >= start && value <= end;
		}

		double clamp(double value)
		{
			return value < start ? start
					     : (value > end ? end : value);
		}

		double len() const { return end - start; }

		double start, end;
	};

	Pwl() {}
	Pwl(std::vector<PointF> const &points)
		: points_(points) {}
	int readYaml(const libcamera::YamlObject &params);

	void append(double x, double y, const double eps = 1e-6);
	void prepend(double x, double y, const double eps = 1e-6);

	Interval domain() const;
	Interval range() const;

	bool empty() const;

	double eval(double x, int *spanPtr = nullptr,
		    bool updateSpan = true) const;

	PerpType invert(PointF const &xy, PointF &perp, int &span,
			const double eps = 1e-6) const;
	Pwl inverse(bool *trueInverse = nullptr, const double eps = 1e-6) const;
	Pwl compose(Pwl const &other, const double eps = 1e-6) const;

	void map(std::function<void(double x, double y)> f) const;

	static void map2(Pwl const &pwl0, Pwl const &pwl1,
			 std::function<void(double x, double y0, double y1)> f);

	static Pwl
	combine(Pwl const &pwl0, Pwl const &pwl1,
		std::function<double(double x, double y0, double y1)> f,
		const double eps = 1e-6);

	void extendDomain(Interval const &domain, bool clip = true,
			  const double eps = 1e-6);

	Pwl &operator*=(double d);

	std::string toString() const;

private:
	int findSpan(double x, int span) const;
	std::vector<PointF> points_;
};

} /* namespace ipa */

} /* namespace libcamera */
