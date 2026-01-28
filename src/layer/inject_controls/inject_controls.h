/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * Copyright (C) 2025, Ideas On Board Oy
 *
 * Layer implementation for injecting controls
 */

#pragma once

#include <libcamera/controls.h>
#include <libcamera/request.h>

struct InjectControls {
	bool aeAvailable;
	bool meAvailable;
	bool agAvailable;
	bool mgAvailable;
};

void *init(const std::string &id);
void terminate(void *closure);
libcamera::ControlInfoMap::Map updateControls(void *closure, libcamera::ControlInfoMap &ctrls);
void queueRequest(void *closure, libcamera::Request *request);
void requestCompleted([[maybe_unused]] void *closure, libcamera::Request *request);
