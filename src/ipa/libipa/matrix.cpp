/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * Copyright (C) 2024, Paul Elder <paul.elder@ideasonboard.com>
 *
 * Matrix and related operations
 */

#include "matrix.h"

#include <libcamera/base/log.h>

/**
 * \file matrix.h
 * \brief Matrix class
 */

namespace libcamera {

LOG_DEFINE_CATEGORY(Matrix)

namespace ipa {

/**
 * \class Matrix
 * \brief Matrix class
 * \tparam T Type of numerical values to be stored in the matrix
 * \tparam R Number of rows in the matrix
 * \tparam C Number of columns in the matrix
 */

/**
 * \fn Matrix::Matrix()
 * \brief Construct an identity matrix
 */

/**
 * \fn Matrix::Matrix(const std::vector<T> &data)
 * \brief Construct matrix from supplied data
 * \param data Data from which to construct a matrix
 *
 * \a data is a one-dimensional vector and will be turned into a matrix in
 * row-major order. The size of \a data must be equal to the product of the
 * number of rows and columns of the matrix (RxC).
 */

/**
 * \fn Matrix::readYaml
 * \brief Populate the matrix with yaml data
 * \param yaml Yaml data to populate the matrix with
 *
 * Any existing data in the matrix will be overwritten. The size of the data
 * read from \a yaml must be equal to the product of the number of rows and
 * columns of the matrix (RxC).
 *
 * The yaml data is expected to be a list with elements of type T.
 *
 * \return 0 on success, negative error code otherwise
 */

/**
 * \fn Matrix::toString
 * \brief Assemble and return a string describing the matrix
 * \return A string describing the matrix
 */

/**
 * \fn Span<const T, C> Matrix::operator[](size_t i) const
 * \brief Index to a row in the matrix
 * \param i Index of row to retrieve
 *
 * This operator[] returns a Span, which can then be indexed into again with
 * another operator[], allowing a convenient m[i][j] to access elements of the
 * matrix. Note that the lifetime of the Span returned by this first-level
 * operator[] is bound to that of the Matrix itself, so it is not recommended
 * to save the Span that is the result of this operator[].
 *
 * \return Row \a i from the matrix, as a Span
 */

/**
 * \fn Matrix::operator[](size_t i)
 * \copydoc Matrix::operator[](size_t i) const
 */

/**
 * \fn Matrix::Matrix<U, R, C> operator*(T d, const Matrix<U, R, C> &m)
 * \brief Scalar product
 * \tparam T Type of the numerical scalar value
 * \tparam U Type of numerical values in the matrix
 * \tparam R Number of rows in the matrix
 * \tparam C Number of columns in the matrix
 * \param d Scalar
 * \param m Matrix
 * \return Product of scalar \a d and matrix \a m
 */

/**
 * \fn Matrix<T, R1, C2> operator*(const Matrix<T, R1, C1> &m1, const Matrix<T, R2, C2> &m2)
 * \brief Matrix multiplication
 * \tparam T Type of numerical values in the matrices
 * \tparam R1 Number of rows in the first matrix
 * \tparam C1 Number of columns in the first matrix
 * \tparam R2 Number of rows in the second matrix
 * \tparam C2 Number of columns in the second matrix
 * \param m1 Multiplicand matrix
 * \param m2 Multiplier matrix
 * \return Matrix product of matrices \a m1 and \a m2
 */

/**
 * \fn Matrix<T, R, C> operator+(const Matrix<T, R, C> &m1, const Matrix<T, R, C> &m2)
 * \brief Matrix addition
 * \tparam T Type of numerical values in the matrices
 * \tparam R Number of rows in the matrices
 * \tparam C Number of columns in the matrices
 * \param m1 Summand matrix
 * \param m2 Summand matrix
 * \return Matrix sum of matrices \a m1 and \a m2
 */

} /* namespace ipa */

} /* namespace libcamera */
