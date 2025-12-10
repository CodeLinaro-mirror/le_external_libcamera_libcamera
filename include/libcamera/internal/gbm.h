/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * Copyright (C) 2024, Linaro Ltd.
 *
 * Authors:
 * Bryan O'Donoghue <bryan.odonoghue@linaro.org>
 *
 * Helper class for managing GBM interactions
 */

#pragma once

#include <gbm.h>

#include <libcamera/base/log.h>

#include <libcamera/formats.h>

namespace libcamera {

LOG_DECLARE_CATEGORY(GBM)

class GBM
{
public:
	GBM();
	~GBM();

	int createDevice();
	struct gbm_device *getDevice();
	PixelFormat getPixelFormat();

private:
	int fd_;
	struct gbm_device *gbmDevice_;
	PixelFormat format_;
};

} /* namespace libcamera */
