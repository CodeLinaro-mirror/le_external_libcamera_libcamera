/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * Copyright (C) 2024, Ideas on Board Oy
 *
 * libipa miscellaneous helpers
 */

#pragma once

#include <stdint.h>

namespace libcamera {

namespace ipa {

unsigned int rec601LuminanceFromRGB(unsigned int r, unsigned int g, unsigned int b);
uint32_t estimateCCT(double red, double green, double blue);
unsigned int toQFormat(double value, unsigned int m, unsigned int n);
double fromQFormat(unsigned int value, unsigned int n);

} /* namespace ipa */

} /* namespace libcamera */
