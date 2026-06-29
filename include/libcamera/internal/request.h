/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * Copyright (C) 2019, Google Inc.
 *
 * Request class private data
 */

#pragma once

#include <stdint.h>
#include <unordered_set>

#include <libcamera/base/event_notifier.h>

#include <libcamera/request.h>

namespace libcamera {

class Camera;
class FrameBuffer;

class Request::Private : public Extensible::Private
{
	LIBCAMERA_DECLARE_PUBLIC(Request)

public:
	Private(Camera *camera);
	~Private();

	Camera *camera() const { return camera_; }
	bool hasPendingBuffers() const { return !pending_.empty(); }

	ControlList &metadata() { return metadata_; }

	bool completeBuffer(FrameBuffer *buffer);
	void complete();
	void cancel();
	void reset();

private:
	friend class PipelineHandler;
	friend std::ostream &operator<<(std::ostream &out, const Request &r);

	void doCancelRequest();

	Camera *camera_;
	bool cancelled_;
	uint32_t sequence_ = 0;

	std::unordered_set<FrameBuffer *> pending_;
	ControlList metadata_;
};

} /* namespace libcamera */
