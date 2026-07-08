/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * Copyright (C) 2024, Ideas On Board
 *
 * Polynomial based lens shading correction
 */
#pragma once

#include <array>
#include <map>
#include <tuple>
#include <vector>

#include <libcamera/base/span.h>

#include <libcamera/geometry.h>

#include "libcamera/internal/value_node.h"

#include "lsc_base.h"

namespace libcamera {

namespace ipa {

namespace lsc {

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

} /* namespace lsc */

class LscPolynomialBase
{
private:
	using PolynomialComponents = std::map<std::string, lsc::Polynomial>;
	using PolynomialComponentsMap = std::map<unsigned int, PolynomialComponents>;

protected:
	int parseLscData(const ValueNode &yamlSets,
			 const LscDescriptor &descriptor);

	PolynomialComponentsMap lscData_;
};

template<typename U>
class LscPolynomial : public LscPolynomialBase, public LscImplementation<U>
{
public:
	int parseLscData(const ValueNode &yamlSets,
			 const LscDescriptor &descriptor) override
	{
		return LscPolynomialBase::parseLscData(yamlSets, descriptor);
	}

	lsc::ComponentsMap
	sampleForCrop(const Rectangle &cropRectangle,
		      std::vector<double> xPos, std::vector<double> yPos) override
	{
		lsc::ComponentsMap components;

		for (const auto &[t, c] : lscData_) {
			lsc::Components comp;

			for (const auto &[k, p] : c) {
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
	std::vector<uint16_t> samplePolynomial(const lsc::Polynomial &poly,
					       Span<const double> xPositions,
					       Span<const double> yPositions,
					       const Rectangle &cropRectangle)
	{
		double m = poly.getM();
		double x0 = cropRectangle.x / m;
		double y0 = cropRectangle.y / m;
		double w = cropRectangle.width / m;
		double h = cropRectangle.height / m;
		std::vector<uint16_t> samples;

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

} /* namespace libcamera */
