/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * Copyright (C) 2019, Google Inc.
 *
 * Capture request handling
 */

#pragma once

#include <map>
#include <memory>
#include <ostream>
#include <stdint.h>
#include <string>

#include <libcamera/base/class.h>
#include <libcamera/base/signal.h>

#include <libcamera/controls.h>
#include <libcamera/fence.h>

namespace libcamera {

class Camera;
class FrameBuffer;
class Stream;

class Request : public Extensible
{
	LIBCAMERA_DECLARE_PRIVATE()

public:
	enum Status {
		RequestPending,
		RequestComplete,
		RequestCancelled,
	};

	enum ReuseFlag {
		Default = 0,
		ReuseBuffers = (1 << 0),
	};

	using BufferMap = std::map<const Stream *, FrameBuffer *>;

	Request(Camera *camera, uint64_t cookie = 0);
	~Request();

	void reuse(ReuseFlag flags = Default);

	ControlList &controls();
	const ControlList &metadata() const;
	const BufferMap &buffers() const;
	int addBuffer(const Stream *stream, FrameBuffer *buffer,
		      std::unique_ptr<Fence> &&fence = {});
	FrameBuffer *findBuffer(const Stream *stream) const;

	uint32_t sequence() const;
	uint64_t cookie() const;
	Status status() const;

	bool hasPendingBuffers() const;

	std::string toString() const;

private:
	LIBCAMERA_DISABLE_COPY(Request)
};

std::ostream &operator<<(std::ostream &out, const Request &r);

} /* namespace libcamera */
