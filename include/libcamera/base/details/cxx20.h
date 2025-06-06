/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * Copyright (C) 2025, Ideas on Board Oy
 *
 * C++20 polyfills
 */

#pragma once

namespace libcamera::details::cxx20 {

template<typename T> struct type_identity { using type = T; };
template<typename T> using type_identity_t = typename type_identity<T>::type;

} /* namespace libcamera::details::cxx20 */
