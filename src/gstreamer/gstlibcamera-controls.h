/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * Copyright (C) 2023, Collabora Ltd.
 *     Author: Nicolas Dufresne <nicolas.dufresne@collabora.com>
 *
 * gstlibcamera-controls.h - GStreamer Camera Controls
 */

#pragma once

#include <libcamera/controls.h>
#include <libcamera/request.h>

#include "gstlibcamerasrc.h"

namespace libcamera {

class GstCameraControls
{
public:
	GstCameraControls() {};
	~GstCameraControls() {};

	static void installProperties(GObjectClass *klass, int lastProp);

	bool getProperty(guint propId, GValue *value, GParamSpec *pspec);
	bool setProperty(guint propId, const GValue *value, GParamSpec *pspec);

	void applyControls(std::unique_ptr<libcamera::Request> &request);

private:
	/* set of user modified controls */
	ControlList controls_;
};

} /* namespace libcamera */
