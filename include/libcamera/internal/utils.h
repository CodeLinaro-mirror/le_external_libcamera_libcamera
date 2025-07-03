/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * Copyright (C) 2018, Google Inc.
 *
 * Miscellaneous utility functions
 */

#pragma once

#include <functional>
#include <string>
#include <vector>

namespace libcamera {

namespace utils {

void parseDir(const char *libDir, unsigned int maxDepth,
	      std::vector<std::string> &files);

unsigned int addDir(const char *libDir, unsigned int maxDepth,
		    std::function<int(const std::string &)> func);

} /* namespace utils */

} /* namespace libcamera */
