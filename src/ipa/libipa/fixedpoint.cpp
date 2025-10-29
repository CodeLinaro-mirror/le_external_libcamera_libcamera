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
 * \fn R floatingToFixedPoint(T number)
 * \brief Convert a floating point number to a fixed-point representation
 * \tparam I Bit width of the integer part of the fixed-point
 * \tparam F Bit width of the fractional part of the fixed-point
 * \tparam R Return type of the fixed-point representation
 * \tparam T Input type of the floating point representation
 * \param number The floating point number to convert to fixed point
 * \return The converted value
 */

/**
 * \fn R fixedToFloatingPoint(T number)
 * \brief Convert a fixed-point number to a floating point representation
 * \tparam I Bit width of the integer part of the fixed-point
 * \tparam F Bit width of the fractional part of the fixed-point
 * \tparam R Return type of the floating point representation
 * \tparam T Input type of the fixed-point representation
 * \param number The fixed point number to convert to floating point
 * \return The converted value
 */

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
 * \typedef FixedPointQTraits::quantized_type
 * \brief The integral storage type used for the fixed-point representation
 */

/**
 * \var FixedPointQTraits::Bits
 * \brief Total number of bits used in the fixed-point format (I + F)
 */

/**
 * \var FixedPointQTraits::BitMask
 * \brief Bit mask selecting all valid bits in the fixed-point representation
 */

/**
 * \var FixedPointQTraits::qmin
 * \brief Minimum representable quantised integer value
 *
 * This corresponds to the most negative value for signed formats or zero for
 * unsigned formats.
 */

/**
 * \var FixedPointQTraits::qmax
 * \brief Maximum representable quantised integer value
 */

/**
 * \var FixedPointQTraits::min
 * \brief Minimum representable floating-point value corresponding to qmin
 */

/**
 * \var FixedPointQTraits::max
 * \brief Maximum representable floating-point value corresponding to qmax
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
 * \fn FixedPointQTraits::toFloat(quantized_type q)
 * \brief Convert a fixed-point integer to a floating-point value
 * \param[in] q The fixed-point integer value to be converted
 * \return The corresponding floating-point value
 *
 * The conversion sign-extends the integer value if required and divides by the
 * scaling factor defined by the number of fractional bits F.
 */

/**
 * \typedef Q1_7
 * \brief 1.7 signed fixed-point quantizer
 *
 * A Quantized type using 1 bit for the integer part and 7 bits for the
 * fractional part, stored in a signed 8-bit integer (\c int8_t). Represents
 * values approximately in the range [-1.0, 0.992] with a resolution of 1/128.
 */

/**
 * \typedef UQ1_7
 * \brief 1.7 unsigned fixed-point quantizer
 *
 * A Quantized type using 1 bit for the integer part and 7 bits for the
 * fractional part, stored in an unsigned 8-bit integer (\c uint8_t). Represents
 * values in the range [0.0, 1.992] with a resolution of 1/128.
 */

/**
 * \typedef Q12_4
 * \brief 12.4 signed fixed-point quantizer
 *
 * A Quantized type using 12 bits for the integer part and 4 bits for the
 * fractional part, stored in a signed 16-bit integer (\c int16_t). Represents
 * values in the range approximately [-2048.0, 2047.9375] with a resolution of
 * 1/16.
 */

/**
 * \typedef UQ12_4
 * \brief 12.4 unsigned fixed-point quantizer
 *
 * A Quantized type using 12 bits for the integer part and 4 bits for the
 * fractional part, stored in an unsigned 16-bit integer (\c uint16_t).
 * Represents values in the range [0.0, 4095.9375] with a resolution of 1/16.
 */

/**
 * \struct ScaledFixedPointQTraits
 * \brief Wrap a FixedPointQTraits with a linear scaling factor
 *
 * This trait extends an existing fixed-point quantisation policy
 * by applying an additional multiplicative scale between the
 * floating-point and quantised domains.
 *
 * \tparam Q The base fixed-point traits type
 * \tparam Scale The scale factor applied to the floating-point domain
 */

/**
 * \typedef ScaledFixedPointQTraits::quantized_type
 * \copydoc FixedPointQTraits::quantized_type
 */

/**
 * \var ScaledFixedPointQTraits::scale
 * \brief The constant scaling factor applied to the floating-point domain.
 *
 * Floating-point inputs are divided by this factor before quantisation,
 * and multiplied by it after dequantisation.
 */

/**
 * \var ScaledFixedPointQTraits::qmin
 * \copydoc FixedPointQTraits::qmin
 */

/**
 * \var ScaledFixedPointQTraits::qmax
 * \copydoc FixedPointQTraits::qmax
 */

/**
 * \var ScaledFixedPointQTraits::min
 * \copydoc FixedPointQTraits::min
 */

/**
 * \var ScaledFixedPointQTraits::max
 * \copydoc FixedPointQTraits::max
 */

/**
 * \fn ScaledFixedPointQTraits::fromFloat(float v)
 * \copydoc FixedPointQTraits::fromFloat(float v)
 *
 * The input value \a v is divided by the scaling factor before conversion.
 */

/**
 * \fn ScaledFixedPointQTraits::toFloat(quantized_type q)
 * \copydoc FixedPointQTraits::toFloat(quantized_type q)
 *
 * The output value is multiplied by the scaling factor after conversion.
 */

} /* namespace ipa */

} /* namespace libcamera */
