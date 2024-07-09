/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (C) 2024, Ideas On Board Oy
 *
 * i.MX8MP Dewarp Engine integration
 */

#include "libcamera/internal/converter/converter_dw100.h"

#include <libcamera/base/log.h>

#include <libcamera/geometry.h>

#include "libcamera/internal/media_device.h"
#include "libcamera/internal/v4l2_videodevice.h"

namespace libcamera {

LOG_DECLARE_CATEGORY(Converter)

/**
 * \class libcamera::ConverterDW100
 * \brief The i.MX8MP dewarp converter implements the converter interface based
 * on V4L2 M2M device.
*/

/**
 * \fn ConverterDW100::ConverterDW100
 * \brief Construct a ConverterDW100 instance
 * \param[in] media The media device implementing the converter
 */
ConverterDW100::ConverterDW100(std::shared_ptr<MediaDevice> media)
	: V4L2M2MConverter(media.get(), Feature::Crop)
{
}

} /* namespace libcamera */
