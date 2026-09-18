/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * Copyright (C) 2020, Google Inc.
 *
 * Miscellaneous utility functions to access sysfs
 */

#pragma once

#include <string>

namespace libcamera {

namespace sysfs {

std::string charDevPath(const std::string &deviceNode);

std::string devicePath(const std::string &deviceNode);
std::string devicePath(unsigned int deviceMajor, unsigned int deviceMinor);

std::string firmwareNodePath(const std::string &device);

} /* namespace sysfs */

} /* namespace libcamera */
