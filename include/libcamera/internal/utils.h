/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * Copyright (C) 2018, Google Inc.
 *
 * Miscellaneous utility functions
 */

#pragma once

#include <functional>
#include <stdint.h>
#include <string>
#include <vector>

#include <libcamera/base/span.h>

namespace libcamera {

namespace utils {

unsigned int findSharedObjects(const char *libDir, unsigned int maxDepth,
			       std::function<int(const std::string &)> func);

int elfVerifyIdent(Span<const uint8_t> elf);
Span<const uint8_t> elfLoadSymbol(Span<const uint8_t> elf, const char *symbol);

} /* namespace utils */

} /* namespace libcamera */
