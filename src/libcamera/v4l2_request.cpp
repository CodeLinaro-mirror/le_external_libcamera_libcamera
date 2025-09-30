/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * Copyright (C) 2025, Ideas On Board
 *
 * V4L2 Request API
 */

#include "libcamera/internal/v4l2_request.h"

#include <fcntl.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <sys/syscall.h>
#include <unistd.h>

#include <linux/media.h>

#include <libcamera/base/event_notifier.h>
#include <libcamera/base/log.h>

/**
 * \file v4l2_request.h
 * \brief V4L2 Request
 */

namespace libcamera {

LOG_DECLARE_CATEGORY(V4L2)

/**
 * \class V4L2Request
 * \brief V4L2Request object and API
 *
 * The V4L2Request class wraps a V4L2 request fd and provides some convenience
 * functions to handle request.
 *
 * It is usually constructed by calling \a MediaDevice::allocateRequests().
 *
 * A request can then be passed to the V4L2Device::setControls(),
 * V4L2Device::getControls() and V4L2VideoDevice::queueBuffer().
 */

/**
 * \brief Construct a V4L2Request
 * \param[in] fd The request fd
 */
V4L2Request::V4L2Request(int fd)
	: fd_(fd), fdNotifier_(fd, EventNotifier::Exception)
{
	if (!fd_.isValid())
		return;

	fdNotifier_.activated.connect(this, &V4L2Request::requestReady);
	fdNotifier_.setEnabled(false);
}

/**
 * \brief Reinit the request
 *
 * Calls MEDIA_REQUEST_IOC_REINIT om the request fd.
 *
 * \return 0 on success or a negative error code otherwise
 */
int V4L2Request::reinit()
{
	fdNotifier_.setEnabled(false);

	if (::ioctl(fd_.get(), MEDIA_REQUEST_IOC_REINIT) < 0)
		return -errno;

	return 0;
}

/**
 * \brief Reinit the request
 *
 * Calls MEDIA_REQUEST_IOC_QUEUE om the request fd.
 *
 * \return 0 on success or a negative error code otherwise
 */
int V4L2Request::queue()
{
	if (::ioctl(fd_.get(), MEDIA_REQUEST_IOC_QUEUE) < 0)
		return -errno;

	fdNotifier_.setEnabled(true);

	return 0;
}

std::string V4L2Request::logPrefix() const
{
	return "Request [" + std::to_string(fd()) + "]";
}

/**
 * \brief Slot to handle request done events
 *
 * When this slot is called, the request is done and the requestDone will be
 * emitted.
 */
void V4L2Request::requestReady()
{
	requestDone.emit(this);
}

} /* namespace libcamera */
