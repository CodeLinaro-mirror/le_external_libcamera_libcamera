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
 * \tparam I Bit width of the integer part of the fixed-point including the
 * optional sign bit
 * \tparam F Bit width of the fractional part of the fixed-point
 * \tparam R Return type of the floating point representation
 * \tparam T Input type of the fixed-point representation
 * \param number The fixed point number to convert to floating point
 *
 * If the fixed-point representation is signed, the sign bit shall be included
 * in the \a I template parameter that specifies the number of bits of the
 * integral part of the fixed-point representation.
 *
 * As an example, a value represented as signed fixed-point Q4.8 format can be
 * converted to its corresponding floating point representation as:
 *
 * \code{.cpp}
 * double d = fixedToFloatingPoint<5, 8, double, uint16_t>(fixed);
 * \endcode
 *
 * While a value represented as unsigned fixed-point Q4.8 format can be
 * converted as:
 *
 * \code{.cpp}
 * double d = fixedToFloatingPoint<4, 8, double, uint16_t>(fixed);
 * \endcode
 *
 * \return The converted value
 */

} /* namespace ipa */

} /* namespace libcamera */
