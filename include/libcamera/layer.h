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

namespace libcamera {

class CameraConfiguration;
class ControlInfoMap;
class ControlList;
class FrameBuffer;
class Request;
class Stream;
enum class StreamRole;

struct Layer
{
	const char *name;
	int layerAPIVersion;

	void (*init)(const std::string &id);

	void (*bufferCompleted)(Request *, FrameBuffer *);
	void (*requestCompleted)(Request *);
	void (*disconnected)();

	void (*acquire)();
	void (*release)();

	ControlInfoMap::Map (*controls)(ControlInfoMap &);
	ControlList (*properties)(ControlList &);
	std::set<Stream *> (*streams)(std::set<Stream *> &);

	void (*generateConfiguration)(Span<const StreamRole> &,
				      CameraConfiguration *);

	void (*configure)(CameraConfiguration *);

	void (*createRequest)(uint64_t, Request *);

	void (*queueRequest)(Request *);

	void (*start)(const ControlList *);
	void (*stop)();
} __attribute__((packed));

} /* namespace libcamera */
