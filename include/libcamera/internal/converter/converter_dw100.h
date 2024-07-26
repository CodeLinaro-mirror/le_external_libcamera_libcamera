/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (C) 2024, Ideas On Board Oy
 *
 * i.MX8MP Dewarp Engine integration
 */

#pragma once

#include "libcamera/internal/converter/converter_v4l2_m2m.h"

namespace libcamera {

class MediaDevice;
class Rectangle;
class Stream;

class ConverterDW100 : public V4L2M2MConverter
{
public:
	ConverterDW100(std::shared_ptr<MediaDevice> media);
};

} /* namespace libcamera */
