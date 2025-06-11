/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * Copyright (C) 2024, Linaro Ltd.
 *
 * Authors:
 * Bryan O'Donoghue <bryan.odonoghue@linaro.org>
 *
 * egl.cpp - Helper class for managing GBM interactions.
 */

#include "libcamera/internal/gbm.h"

#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

#include <linux/dma-buf.h>
#include <linux/dma-heap.h>

namespace libcamera {

LOG_DEFINE_CATEGORY(GBM)

GBM::GBM()
{
	fd_ = 0;
}

GBM::~GBM()
{
	if (gbm_surface_)
		gbm_surface_destroy(gbm_surface_);

	if (gbm_device_)
		gbm_device_destroy(gbm_device_);

	if (fd_ >= 0)
		close(fd_);
}

// this should probably go into its own class to deal with the
// allocation and deletion of frambuffers attached to GBM devices/objects
int GBM::initSurface(uint32_t width, uint32_t height)
{
	const char *dri_node = "/dev/dri/renderD128"; //TODO: get from an env or config setting

	fd_ = open(dri_node, O_RDWR | O_CLOEXEC); //TODO: CLOEXEC ?
	if (fd_ < 0) {
		LOG(GBM, Error) << "Open " << dri_node << " fail " << fd_;
		return fd_;
	}

	gbm_device_ = gbm_create_device(fd_);
	if (!gbm_device_) {
		LOG(GBM, Error) << "gbm_crate_device fail";
		goto fail;
	}

	// GBM_FORMAT_RGBA8888 is not supported mesa::src/gbm/dri/gbm_dri.c::gbm_dri_visuals_table[]
	// This means we need to choose XRGB8888 or ARGB8888 as the raw buffer format
	gbm_surface_ = gbm_surface_create(gbm_device_, width, height, GBM_FORMAT_ARGB8888,
					  GBM_BO_USE_RENDERING | GBM_BO_USE_LINEAR);
	if (!gbm_surface_) {
		LOG(GBM, Error) << "Unable to create linear gbm surface";
		goto fail;
	}

	format_ = libcamera::formats::ARGB8888;

	return 0;
fail:
	return -ENODEV;
}

int GBM::mapSurface()
{
	gbm_bo_ = gbm_surface_lock_front_buffer(gbm_surface_);
	if (!gbm_bo_) {
		LOG(GBM, Error) << "GBM input buffer object create fail";
		return -ENODEV;
	}
	gbm_surface_release_buffer(gbm_surface_, gbm_bo_);

	bo_fd_ = gbm_bo_get_fd(gbm_bo_);

	if (!bo_fd_) {
		gbm_surface_release_buffer(gbm_surface_, gbm_bo_);
		LOG(GBM, Error) << "Unable to get fd for bo: " << bo_fd_;
		return -ENODEV;
	}

	stride_ = gbm_bo_get_stride(gbm_bo_);
	width_ = gbm_bo_get_width(gbm_bo_);
	height_ = gbm_bo_get_height(gbm_bo_);
	offset_ = gbm_bo_get_offset(gbm_bo_, 0);
	framesize_ = height_ * stride_;

	map_ = mmap(NULL, height_ * stride_, PROT_READ, MAP_SHARED, bo_fd_, 0);
	if (map_ == MAP_FAILED) {
		LOG(GBM, Error) << "mmap gbm_bo_ fail";
		return -ENODEV;
	}

	LOG(GBM, Debug) << " stride " << stride_
			<< " width " << width_
			<< " height " << height_
			<< " offset " << offset_
			<< " framesize " << framesize_;

	return 0;
}

int GBM::getFrameBufferData(uint8_t *data, size_t data_len)
{
	struct dma_buf_sync sync;

	gbm_surface_lock_front_buffer(gbm_surface_);

	sync.flags = DMA_BUF_SYNC_START | DMA_BUF_SYNC_READ;
	ioctl(bo_fd_, DMA_BUF_IOCTL_SYNC, &sync);

	if (data_len > framesize_) {
		LOG(GBM, Error) << "Invalid read size " << data_len << " max is " << framesize_;
		return -EINVAL;
	}

	memcpy(data, map_, data_len);

	sync.flags = DMA_BUF_SYNC_END | DMA_BUF_SYNC_READ;
	ioctl(bo_fd_, DMA_BUF_IOCTL_SYNC, &sync);

	gbm_surface_release_buffer(gbm_surface_, gbm_bo_);

	return 0;
}
} //namespace libcamera
