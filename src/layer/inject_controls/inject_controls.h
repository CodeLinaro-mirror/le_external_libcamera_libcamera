/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * Copyright (C) 2025, Ideas On Board Oy
 *
 * Layer implementation for injecting controls
 */

#pragma once

#include <libcamera/controls.h>
#include <libcamera/request.h>

namespace layer {

namespace inject_controls {

libcamera::ControlInfoMap::Map controls(libcamera::ControlInfoMap &ctrls);
void queueRequest(libcamera::Request *);
void requestCompleted(libcamera::Request *);

bool initialized_ = false;
bool aeAvailable_ = false;
bool meAvailable_ = false;
bool agAvailable_ = false;
bool mgAvailable_ = false;

} /* namespace inject_controls */

} /* namespace layer */
