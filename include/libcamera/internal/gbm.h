/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * Copyright (C) 2024, Linaro Ltd.
 *
 * Authors:
 * Bryan O'Donoghue <bryan.odonoghue@linaro.org>
 *
 * gbm.h - Helper class for managing GBM interactions.
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

	int initSurface(uint32_t width, uint32_t height);
	int mapSurface();
	int getFrameBufferData(uint8_t *data_out, size_t data_len);
	struct gbm_device *getDevice() { return gbm_device_; }
	struct gbm_surface *getSurface() { return gbm_surface_; }
	uint32_t getFrameSize() { return framesize_; }
	uint32_t getStride() { return stride_; }
	PixelFormat getPixelFormat() { return format_; }

private:
	int fd_;
	struct gbm_device *gbm_device_;
	struct gbm_surface *gbm_surface_;

	struct gbm_bo *gbm_bo_;
	uint32_t width_;
	uint32_t height_;
	uint32_t stride_;
	uint32_t offset_;
	uint32_t framesize_;
	void *map_;
	int bo_fd_;

	PixelFormat format_;
};

} // namespace libcamera
