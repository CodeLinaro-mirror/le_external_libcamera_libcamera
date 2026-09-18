/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * Copyright (C) 2026, Ideas On Board
 *
 * RPP-X1 control algorithm interface
 */

#pragma once

#include <libipa/algorithm.h>

#include "module.h"

namespace libcamera {

namespace ipa::rppx1 {

using Algorithm = libcamera::ipa::Algorithm<Module>;

} /* namespace ipa::rppx1 */

} /* namespace libcamera */
