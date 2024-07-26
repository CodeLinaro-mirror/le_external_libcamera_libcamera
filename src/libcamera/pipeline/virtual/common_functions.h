/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * Copyright (C) 2023, Google Inc.
 *
 * common_functions.h - Helper that do not depend on any class
 */

#pragma once

#include <filesystem>

namespace libcamera {

std::string getExtension(const std::string &path);

std::size_t numberOfFilesInDirectory(std::filesystem::path path);

} // namespace libcamera
