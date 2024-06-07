/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * Copyright (C) 2019, Raspberry Pi Ltd
 * Copyright (C) 2024, Paul Elder <paul.elder@ideasonboard.com>
 *
 * Vector and related operations
 */

#include "vector.h"

#include <libcamera/base/log.h>

/**
 * \file vector.h
 * \brief Vector class
 */

namespace libcamera {

LOG_DEFINE_CATEGORY(Vector)

namespace ipa {

/**
 * \class Vector
 * \brief Vector class
 * \tparam T Type of numerical values to be stored in the vector
 * \tparam R Number of rows in the vector
 */

/**
 * \fn Vector::Vector()
 * \brief Construct an identity vector
 */

/**
 * \fn Vector::Vector(const std::array<T, R> &data)
 * \brief Construct vector from supplied data
 * \param data Data from which to construct a vector
 *
 * \a data is a one-dimensional vector and will be turned into a vector in
 * row-major order. The size of \a data must be equal to the product of the
 * number of rows and columns of the vector (RxC).
 */

/**
 * \fn Vector::readYaml
 * \brief Populate the vector with yaml data
 * \param yaml Yaml data to populate the vector with
 *
 * Any existing data in the vector will be overwritten. The size of the data
 * read from \a yaml must be equal to the product of the number of rows and
 * columns of the vector (RxC).
 *
 * The yaml data is expected to be a list with elements of type T.
 *
 * \return 0 on success, negative error code otherwise
 */

/**
 * \fn Vector::toString
 * \brief Assemble and return a string describing the vector
 * \return A string describing the vector
 */

/**
 * \fn T Vector::operator[](size_t i) const
 * \brief Index to a row in the vector
 * \param i Index of row to retrieve
 *
 * This operator[] returns a Span, which can then be indexed into again with
 * another operator[], allowing a convenient m[i][j] to access elements of the
 * vector. Note that the lifetime of the Span returned by this first-level
 * operator[] is bound to that of the Vector itself, so it is not recommended
 * to save the Span that is the result of this operator[].
 *
 * \return Row \a i from the vector, as a Span
 */

/**
 * \fn T &Vector::operator[](size_t i)
 * \copydoc Vector::operator[](size_t i) const
 */

/**
 * \fn Vector::x()
 * \brief Convenience function to access the first element of the vector
 */

/**
 * \fn Vector::y()
 * \brief Convenience function to access the second element of the vector
 */

/**
 * \fn Vector::operator-() const
 * \brief Negate a Vector by negating both all of its coordinates
 * \return The negated vector
 */

/**
 * \fn Vector::operator-(Vector const &other) const
 * \brief Subtract one vector from another
 * \param[in] other The other vector
 * \return The difference of \a other from this vector
 */

/**
 * \fn Vector::operator+()
 * \brief Add two vectors together
 * \param[in] other The other vector
 * \return The sum of the two vectors
 */

/**
 * \fn Vector::operator*(const Vector<T, R> &other) const
 * \brief Compute the dot product
 * \param[in] other The other vector
 * \return The dot product of the two vectors
 */

/**
 * \fn Vector::operator*(T factor) const
 * \brief Scale up the vector
 * \param[in] factor The factor
 * \return The vector scaled up by \a factor
 */

/**
 * \fn Vector::operator/()
 * \brief Scale down the vector
 * \param[in] factor The factor
 * \return The vector scaled down by \a factor
 */

/**
 * \fn Vector::len2()
 * \brief Get the squared length of the vector
 * \return The squared length of the vector
 */

/**
 * \fn Vector::len()
 * \brief Get the length of the vector
 * \return The length of the vector
 */

/**
 * \fn bool operator==(const Vector<T, R> &lhs, const Vector<T, R> &rhs)
 * \brief Compare vectors for equality
 * \return True if the two vectors are equal, false otherwise
 */

/**
 * \fn bool operator!=(const Vector<T, R> &lhs, const Vector<T, R> &rhs)
 * \brief Compare vectors for inequality
 * \return True if the two vectors are not equal, false otherwise
 */

} /* namespace ipa */

} /* namespace libcamera */
