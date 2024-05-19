/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (C) 2024, Ideas On Board Oy
 *
 * i.MX8MP Dewarp Engine integration
 */

#include "rkisp1_dewarper.h"

#include <libcamera/base/log.h>

#include <libcamera/geometry.h>

#include "libcamera/internal/media_device.h"
#include "libcamera/internal/v4l2_videodevice.h"

namespace libcamera {

LOG_DECLARE_CATEGORY(RkISP1)

RkISP1Dewarper::RkISP1Dewarper(std::shared_ptr<MediaDevice> media)
	: V4L2M2MConverter(media.get())
{
}

int RkISP1Dewarper::setScalerCrop(unsigned int output, Rectangle rect)
{
	int ret;

	ret = setSelection(output, V4L2_SEL_TGT_CROP, &rect);
	if (ret < 0)
		LOG(RkISP1, Error) << "Failed to set scaler crop on dewarper "
				   << strerror(-ret);

	return ret;
}

} /* namespace libcamera */
