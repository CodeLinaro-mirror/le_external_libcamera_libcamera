/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * Copyright (C) 2025, Ideas On Board Oy
 *
 * Layer interface
 */

#pragma once

#include <set>
#include <stdint.h>

#include <libcamera/base/span.h>

#include <libcamera/controls.h>

namespace libcamera {

class CameraConfiguration;
class FrameBuffer;
class Request;
class Stream;
enum class StreamRole;

struct LayerInfo {
	const char *name;
	int layerAPIVersion;
};

struct LayerInterface {
	void *(*init)(const std::string &);
	void (*terminate)(void *);

	void (*bufferCompleted)(void *, Request *, FrameBuffer *);
	void (*requestCompleted)(void *, Request *);
	void (*disconnected)(void *);

	void (*acquire)(void *);
	void (*release)(void *);

	ControlInfoMap::Map (*controls)(void *, ControlInfoMap &);
	ControlList (*properties)(void *, ControlList &);

	void (*configure)(void *, const CameraConfiguration *);

	void (*createRequest)(void *, uint64_t, const Request *);

	void (*queueRequest)(void *, Request *);

	void (*start)(void *, ControlList &);
	void (*stop)(void *);
};

} /* namespace libcamera */
