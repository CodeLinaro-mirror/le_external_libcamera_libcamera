/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * Copyright (C) 2024, Paul Elder <paul.elder@ideasonboard.com>
 *
 * Fixed / floating point conversions
 */

#include "fixedpoint.h"

/**
 * \file fixedpoint.h
 */

namespace libcamera {

namespace ipa {

/**
 * \struct libcamera::ipa::FixedPointQTraits
 * \brief Traits type implementing fixed-point quantisation conversions
 *
 * The FixedPointQTraits structure defines a policy for mapping floating-point
 * values to and from fixed-point integer representations. It is parameterised
 * by the number of integer bits \a I, fractional bits \a F, and the integral
 * storage type \a T. The traits are used with Quantized<Traits> to create a
 * quantised type that stores both the fixed-point representation and the
 * corresponding floating-point value.
 *
 * The trait exposes compile-time constants describing the bit layout, limits,
 * and scaling factors used in the fixed-point representation.
 *
 * \tparam I Number of integer bits
 * \tparam F Number of fractional bits
 * \tparam T Integral type used to store the quantised value
 */

/**
 * \typedef FixedPointQTraits::QuantizedType
 * \brief The integral storage type used for the fixed-point representation
 */

/**
 * \var FixedPointQTraits::qMin
 * \brief Minimum representable quantised integer value
 *
 * This corresponds to the most negative value for signed formats or zero for
 * unsigned formats.
 */

/**
 * \var FixedPointQTraits::qMax
 * \brief Maximum representable quantised integer value
 */

/**
 * \var FixedPointQTraits::min
 * \brief Minimum representable floating-point value corresponding to qMin
 */

/**
 * \var FixedPointQTraits::max
 * \brief Maximum representable floating-point value corresponding to qMax
 */

/**
 * \fn FixedPointQTraits::fromFloat(float v)
 * \brief Convert a floating-point value to a fixed-point integer
 * \param[in] v The floating-point value to be converted
 * \return The quantised fixed-point integer representation
 *
 * The conversion rounds the floating-point input \a v to the nearest integer
 * according to the scaling factor defined by the number of fractional bits F.
 */

/**
 * \fn FixedPointQTraits::toFloat(QuantizedType q)
 * \brief Convert a fixed-point integer to a floating-point value
 * \param[in] q The fixed-point integer value to be converted
 * \return The corresponding floating-point value
 *
 * The conversion sign-extends the integer value if required and divides by the
 * scaling factor defined by the number of fractional bits F.
 */

/**
 * \typedef Q
 * \brief Define a signed fixed-point quantised type with automatic storage width
 * \tparam I The number of integer bits
 * \tparam F The number of fractional bits
 *
 * This alias defines a signed fixed-point quantised type using the
 * \ref FixedPointQTraits trait and a suitable signed integer storage type
 * automatically selected based on the total number of bits \a (I + F).
 */

/**
 * \typedef UQ
 * \brief Define an unsigned fixed-point quantised type with automatic storage width
 * \tparam I The number of integer bits
 * \tparam F The number of fractional bits
 *
 * This alias defines an unsigned fixed-point quantised type using the
 * \ref FixedPointQTraits trait and a suitable unsigned integer storage type
 * automatically selected based on the total number of bits \a (I + F).
 */

} /* namespace ipa */

} /* namespace libcamera */
