/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * Copyright (C) 2025, Ideas On Board.
 *
 * Helper class to manage conversions between floating point types and quantized
 * storage and representation of those values.
 */

#include "quantized.h"

/**
 * \file quantized.h
 * \brief Quantized storage and Quantizer representations
 */

namespace libcamera {

namespace ipa {

/**
 * \struct Quantized
 * \brief Quantized stores both an internal and quantized representation of a value
 * \tparam T The quantized type (e.g., uint16_t, int16_t).
 *
 * This struct provides read-only access to both representations of the value.
 * It does not perform any conversions or quantization logic itself. This must
 * be handled externally by a quantizer that knows the appropriate behaviour.
 *
 * This type can be used to store values that need to be represented in both
 * fixed-point or integer (for hardware interfaces) and floating-point (for user
 * facing interfaces)
 *
 * It is intended to be used in shared context structures where both
 * hardware-level and software-level representations of a value are required.
 */

/**
 * \fn float Quantized::value() const noexcept
 * \brief Get the floating-point representation of the quantized value
 *
 * This function returns the floating-point form that was either directly assigned
 * or computed from the quantized value.
 *
 * \return The floating-point value
 */

/**
 * \fn T Quantized::quantized() const noexcept
 * \brief Get the raw quantized representation of the value
 *
 * This function returns the internal quantized value (e.g. \c uint8_t or \c int16_t),
 * which is typically used for hardware-level programming or serialization.
 *
 * \return The quantized value of type \c T
 */

/**
 * \fn std::string Quantized::toString() const
 * \brief Generate a string representation of the quantized and float values
 *
 * Produces a human-readable string including both the quantized and floating-point
 * values, typically in the format: "Q:0xXX F:float".
 *
 * \return A formatted string representing the internal state
 */

/**
 * \fn bool Quantized::operator==(const Quantized<T> &other) const noexcept
 * \brief Compare two quantized values for equality
 *
 * Two \c Quantized<T> objects are equal if their quantized values are equal.
 *
 * \param other The \c Quantized<T> to compare with
 * \return \c true if equal, \c false otherwise
 */

/**
 * \fn bool Quantized::operator!=(const Quantized<T> &other) const noexcept
 * \brief Compare two quantized values for inequality
 *
 * Two \c Quantized<T> objects are not equal if their quantized values differ.
 *
 * \param other The \c Quantized<T> to compare with
 * \return \c true if not equal, \c false otherwise
 */

/**
 * \var Quantized::quantized_
 * \brief The raw quantized value
 *
 * This member stores the fixed-point or integer representation of the value, typically
 * used for interfacing with hardware or protocols that require compact formats.
 *
 * It must only be updated or accessed through a Quantizer in conjunction with
 * corresponding updates to \c Quantized::value_.
 */

/**
 * \var Quantized::value_
 * \brief The floating-point representation of the quantized value
 *
 * This member holds the floating-point equivalent of the quantized value, usually
 * for use in calculations or presentation to higher-level software components.
 *
 * It must only be updated or accessed through a Quantizer in conjunction with
 * corresponding updates to \c Quantized::quantized_.
 */

/**
 * \class Quantizer
 * \brief Interface for converting between floating-point and quantized types
 * \tparam T The quantized type (e.g., uint16_t, int16_t).
 *
 * This abstract class defines the interface for quantizers that handle
 * conversions between floating-point values and their quantized representations.
 * Specific quantization handlers should implement this interface and provide
 * the toFloat and fromFloat methods specifically for their types.
 */

/**
 * \typedef Quantizer::qType
 * \brief The underlying quantized type used by the quantizer
 *
 * This alias exposes the quantized type (e.g. \c uint8_t or \c int16_t) used internally
 * by the \c Quantizer, which is helpful in template and debug contexts.
 */

/**
 * \fn Quantizer::fromFloat(T val)
 * \brief Convert a floating-point value to its quantized representation
 *
 * \param val The floating-point value
 * \return The quantized equivalent
 */

/**
 * \fn Quantizer::toFloat(T val)
 * \brief Convert a quantized value to its floating-point representation
 *
 * \param val The quantized value
 * \return The floating-point equivalent
 */

/**
 * \fn Quantizer::set(float val)
 * \brief Set the value of a Quantized<T> using a floating-point input
 *
 * This function updates both the quantized and floating-point
 * representations stored in the Quantized<T>, storing the quantized
 * version of the float input.
 *
 * \param val The floating-point value to set
 */

/**
 * \fn Quantizer::set(T val)
 * \brief Set the value of a Quantized<T> using a quantized input
 *
 * This function updates both the quantized and floating-point
 * representations stored in the Quantized<T>.
 *
 * \param val The quantized value to set
 */

} /* namespace ipa */

} /* namespace libcamera */
