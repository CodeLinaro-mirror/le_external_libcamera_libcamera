/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * Copyright (C) 2023, Google Inc.
 *
 * common_functions.cpp - Helper that do not depend on any class
 */

#include "common_functions.h"

namespace libcamera {

std::string getExtension(const std::string &path)
{
	size_t i = path.find(".");
	if (i != std::string::npos) {
		return path.substr(i);
	}
	return "";
}

std::size_t numberOfFilesInDirectory(std::filesystem::path path)
{
	using std::filesystem::directory_iterator;
	return std::distance(directory_iterator(path), directory_iterator{});
}

} // namespace libcamera
