/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (C) 2024, Ideas On Board Oy
 *
 *i.MX8MP Dewarp Engine integration
 */

#pragma once

#include "libcamera/internal/converter/converter_v4l2_m2m.h"

namespace libcamera {

class Rectangle;
struct MediaDevice;

class RkISP1Dewarper : public V4L2M2MConverter
{
public:
	RkISP1Dewarper(std::shared_ptr<MediaDevice> media);

	int setScalerCrop(unsigned int output, Rectangle rect);
};

} /* namespace libcamera */
