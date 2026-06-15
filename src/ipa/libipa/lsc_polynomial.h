/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * Copyright (C) 2026, Ideas On Board
 *
 * Polynomial based lens shading correction
 */

#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <optional>
#include <tuple>

#include <libcamera/base/log.h>
#include <libcamera/base/span.h>

#include <libcamera/geometry.h>

#include "libcamera/internal/value_node.h"

#include "lsc_base.h"

namespace libcamera {

LOG_DECLARE_CATEGORY(LscPolynomial)

namespace ipa {

class Polynomial
{
public:
	Polynomial(double cx = 0.0, double cy = 0.0, double k0 = 0.0,
		   double k1 = 0.0, double k2 = 0.0, double k3 = 0.0,
		   double k4 = 0.0)
		: cx_(cx), cy_(cy), cnx_(0), cny_(0),
		  coefficients_({ k0, k1, k2, k3, k4 })
	{
	}

	Polynomial(const Polynomial &other) = default;

	double sampleAtNormalizedPixelPos(double x, double y) const;
	double getM() const;
	void setReferenceImageSize(const Size &size);

private:
	double cx_;
	double cy_;
	double cnx_;
	double cny_;
	std::array<double, 5> coefficients_;
	Size imageSize_;
};

class LscPolynomialBase
{
private:
	using Components = std::map<std::string, Polynomial>;
	using ComponentsMap = std::map<unsigned int, Components>;

public:
	int parseLscData(const ValueNode &yamlSets,
			 const LscDescriptor &descriptor);

protected:
	ComponentsMap lscData_;
};

template<typename T, typename U>
class LscPolynomial : public LscPolynomialBase, public LscImplementation<T, U>
{
public:
	~LscPolynomial() {}

	int parseLscData(const ValueNode &yamlSets,
			 const LscDescriptor &descriptor) override
	{
		return LscPolynomialBase::parseLscData(yamlSets, descriptor);
	}

	lsc::ComponentsMap<T> resampleLscData(const Rectangle &cropRectangle,
					      const std::vector<double> &xPos,
					      const std::vector<double> &yPos) override
	{
		lsc::ComponentsMap<T> components;

		for (auto &[t, c] : lscData_) {
			lsc::Components<T> comp;

			for (auto &[k, p] : c) {
				comp.emplace(std::piecewise_construct,
					     std::forward_as_tuple(k),
					     std::forward_as_tuple(samplePolynomial(p, xPos, yPos,
										    cropRectangle)));
			}

			components[t] = comp;
		}

		return components;
	}

private:
	std::vector<T> samplePolynomial(const Polynomial &poly,
					Span<const double> xPositions,
					Span<const double> yPositions,
					const Rectangle &cropRectangle)

	{
		double m = poly.getM();
		double x0 = cropRectangle.x / m;
		double y0 = cropRectangle.y / m;
		double w = cropRectangle.width / m;
		double h = cropRectangle.height / m;
		std::vector<T> samples;

		samples.reserve(xPositions.size() * yPositions.size());

		for (double y : yPositions) {
			for (double x : xPositions) {
				double xp = x0 + x * w;
				double yp = y0 + y * h;
				float sample = static_cast<float>
						(poly.sampleAtNormalizedPixelPos(xp, yp));

				samples.push_back(U(sample).quantized());
			}
		}

		return samples;
	}
};

} /* namespace ipa */

#ifndef __DOXYGEN__

template<>
struct ValueNode::Accessor<ipa::Polynomial> {
	std::optional<ipa::Polynomial> get(const ValueNode &obj) const
	{
		std::optional<double> cx = obj["cx"].get<double>();
		std::optional<double> cy = obj["cy"].get<double>();
		std::optional<double> k0 = obj["k0"].get<double>();
		std::optional<double> k1 = obj["k1"].get<double>();
		std::optional<double> k2 = obj["k2"].get<double>();
		std::optional<double> k3 = obj["k3"].get<double>();
		std::optional<double> k4 = obj["k4"].get<double>();

		if (!(cx && cy && k0 && k1 && k2 && k3 && k4))
			LOG(LscPolynomial, Error)
				<< "Polynomial is missing a parameter";

		return ipa::Polynomial(*cx, *cy, *k0, *k1, *k2, *k3, *k4);
	}
};

#endif

} /* namespace libcamera */
