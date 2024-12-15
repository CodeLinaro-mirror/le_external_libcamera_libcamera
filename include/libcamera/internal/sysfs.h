/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * Copyright (C) 2020, Google Inc.
 *
 * Miscellaneous utility functions to access sysfs
 */

#pragma once

#include <string>
#include <string_view>

namespace libcamera {

namespace sysfs {

std::string charDevPath(const std::string &deviceNode);

std::string firmwareNodePath(std::string_view device);

} /* namespace sysfs */

} /* namespace libcamera */
