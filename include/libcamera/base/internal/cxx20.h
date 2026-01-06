/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * Copyright (C) 2025, Ideas on Board Oy
 *
 * C++20 polyfills
 */

#pragma once

/**
 * \internal
 * \file cxx20.h
 * \brief C++17 implementations of certain C++20 types and functions
 */

namespace libcamera::internal::cxx20 {

/**
 * \internal
 * \brief std::type_identity
 *
 * Implementation of std::type_identity for C++17.
 */
template<typename T> struct type_identity {
	/**
	 * \internal
	 * \brief std::type_identity<T>::type
	 *
	 * Type alias matching the template parameter.
	 */
	using type = T;
};

/**
 * \internal
 * \brief std::type_identity_t
 *
 * Implementation of std::type_identity_t for C++17.
 */
template<typename T> using type_identity_t = typename type_identity<T>::type;

} /* namespace libcamera::internal::cxx20 */
