/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * Copyright (C) 2026, Ideas On Board
 *
 * Polynomial based lens shading correction
 */

#include "lsc_polynomial.h"

#include <assert.h>

#include <libcamera/base/log.h>

/**
 * \file lsc_polynomial.h
 * \brief LscPolynomial class
 */

namespace libcamera {

#ifndef __DOXYGEN__
template<>
std::optional<ipa::Polynomial>
ValueNode::Accessor<ipa::Polynomial>::get(const ValueNode &obj) const
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
#endif /* __DOXYGEN__ */

LOG_DEFINE_CATEGORY(LscPolynomial)

namespace ipa {

/**
 * \class Polynomial
 * \brief Class for handling even polynomials used in lens shading correction
 *
 * Shading artifacts of camera lenses can be modeled using even radial
 * polynomials. This class implements a polynomial with 5 coefficients which
 * follows the definition of the FixVignetteRadial opcode in the Adobe DNG
 * specification.
 */

/**
 * \fn Polynomial::Polynomial(double cx = 0.0, double cy = 0.0, double k0 = 0.0,
		      double k1 = 0.0, double k2 = 0.0, double k3 = 0.0,
		      double k4 = 0.0)
 * \brief Construct a polynomial using the given coefficients
 * \param cx Center-x relative to the image in normalized coordinates (0..1)
 * \param cy Center-y relative to the image in normalized coordinates (0..1)
 * \param k0 Coefficient of the polynomial
 * \param k1 Coefficient of the polynomial
 * \param k2 Coefficient of the polynomial
 * \param k3 Coefficient of the polynomial
 * \param k4 Coefficient of the polynomial
 */

/**
 * \fn Polynomial::Polynomial(const Polynomial &other)
 * \brief Construct a Polynomial by copy
 * \param[in] other The Polynomial to copy construct from
 */

/**
 * \fn Polynomial::sampleAtNormalizedPixelPos(double x, double y)
 * \brief Sample the polynomial at the given normalized pixel position
 *
 * This functions samples the polynomial at the given pixel position divided by
 * the value returned by getM().
 *
 * \param x x position in normalized coordinates
 * \param y y position in normalized coordinates
 * \return The sampled value
 */
double Polynomial::sampleAtNormalizedPixelPos(double x, double y) const
{
	double dx = x - cnx_;
	double dy = y - cny_;
	double r = sqrt(dx * dx + dy * dy);
	double res = 1.0;

	for (unsigned int i = 0; i < coefficients_.size(); i++)
		res += coefficients_[i] * std::pow(r, (i + 1) * 2);

	return res;
}

/**
 * \fn Polynomial::getM()
 * \brief Get the value m as described in the dng specification
 *
 * Returns m according to dng spec. m represents the Euclidean distance
 * (in pixels) from the optical center to the farthest pixel in the
 * image.
 *
 * \return The sampled value
 */
double Polynomial::getM() const
{
	double cpx = imageSize_.width * cx_;
	double cpy = imageSize_.height * cy_;
	double mx = std::max(cpx, std::fabs(imageSize_.width - cpx));
	double my = std::max(cpy, std::fabs(imageSize_.height - cpy));

	return sqrt(mx * mx + my * my);
}

/**
 * \fn Polynomial::setReferenceImageSize(const Size &size)
 * \brief Set the reference image size
 *
 * Set the reference image size that is used for subsequent calls to getM() and
 * sampleAtNormalizedPixelPos()
 *
 * \param size The size of the reference image
 */
void Polynomial::setReferenceImageSize(const Size &size)
{
	assert(!size.isNull());
	imageSize_ = size;

	/* Calculate normalized centers */
	double m = getM();
	cnx_ = (size.width * cx_) / m;
	cny_ = (size.height * cy_) / m;
}

/**
 * \class LscPolynomialBase
 * \brief Base class for LscPolynomial
 *
 * Base class for LscPolynomial for non-templated functions.
 */

/**
 * \brief Parse polynomial lsc data
 * \param[in] yamlSets The tuning file content
 * \param[in] descriptor The lsc engine descriptor
 *
 * Parse the lsc data in polyomial form from the \a yamlSet tuning data.
 */
int LscPolynomialBase::parseLscData(const ValueNode &yamlSets,
				    const LscDescriptor &descriptor)
{
	const auto &sets = yamlSets.asList();
	for (const auto &yamlSet : sets) {
		uint32_t ct = yamlSet["ct"].get<uint32_t>(0);

		Components components;
		for (auto &k : descriptor.keys) {
			auto polynomial = yamlSet[k.c_str()].get<Polynomial>();
			if (!polynomial) {
				LOG(LscPolynomial, Error)
					<< "Missing polynomial for component "
					<< k;
				return -EINVAL;
			}

			auto [it, inserted] =
				components.emplace(std::piecewise_construct,
						   std::forward_as_tuple(k.c_str()),
						   std::forward_as_tuple(*polynomial));

			it->second.setReferenceImageSize(descriptor.sensorSize);
		}

		auto [it, inserted] = lscData_.emplace(ct, components);
		if (!inserted) {
			LOG(LscPolynomial, Error)
				<< "Multiple sets found for "
				<< "color temperature " << ct;
			return -EINVAL;
		}
	}

	if (lscData_.empty()) {
		LOG(LscPolynomial, Error) << "Failed to load any sets";
		return -EINVAL;
	}

	return 0;
}

/**
 * \var LscPolynomialBase::lscData_
 * \brief The polynomial lsc data
 *
 * Maps colour temperatures to per-colour radial polynomial definitions.
 */

/**
 * \class LscPolynomial
 * \brief Radial Polynomial lsc algorithm implementation
 *
 * Polynomial-based lsc algorithm implementation. The LscPolynomial class
 * implements lsc support using Polynomial to represent the shading artifacts
 * map.
 *
 * \sa LscImplementation
 * \sa LscAlgorithm
 *
 */

} /* namespace ipa */
} /* namespace libcamera */
