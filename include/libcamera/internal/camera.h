/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * Copyright (C) 2021, Google Inc.
 *
 * Camera private data
 */

#pragma once

#include <atomic>
#include <list>
#include <memory>
#include <queue>
#include <set>
#include <stdint.h>
#include <string>
#include <unordered_map>
#include <vector>

#include <libcamera/base/class.h>
#include <libcamera/base/event_notifier.h>

#include <libcamera/camera.h>

namespace libcamera {

class CameraControlValidator;
class Fence;
class PipelineHandler;
class Stream;

class Camera::Private : public Extensible::Private
{
	LIBCAMERA_DECLARE_PUBLIC(Camera)

public:
	Private(PipelineHandler *pipe);
	~Private();

	PipelineHandler *pipe() { return pipe_.get(); }
	const PipelineHandler *pipe() const { return pipe_.get(); }

	std::list<Request *> queuedRequests_;
	std::queue<Request *> waitingRequests_;
	ControlInfoMap controlInfo_;
	ControlList properties_;

	uint32_t requestSequence_;

	const CameraControlValidator *validator() const { return validator_.get(); }

#ifndef __DOXYGEN__
	struct FrameBufferPoolDeleter {
		std::vector<FrameBuffer *> *pool = nullptr;
		void operator()(FrameBuffer *buffer) const;
	};
#endif

	using PooledFrameBuffer = std::unique_ptr<FrameBuffer, FrameBufferPoolDeleter>;

	[[nodiscard]] PooledFrameBuffer acquireBuffer(const Stream *stream);

private:
	enum State {
		CameraAvailable,
		CameraAcquired,
		CameraConfigured,
		CameraStopping,
		CameraRunning,
	};

	struct StreamData {
		bool active = false;
		std::vector<FrameBuffer *> buffers;
	};

	struct PendingFence {
		EventNotifier notifier;
		const Stream *stream;
		FrameBuffer *buffer;

		PendingFence(const Stream *s, FrameBuffer *b);
	};

	bool isAcquired() const;
	bool isRunning() const;
	int isAccessAllowed(State state, bool allowDisconnected = false,
			    const char *from = __builtin_FUNCTION()) const;
	int isAccessAllowed(State low, State high,
			    bool allowDisconnected = false,
			    const char *from = __builtin_FUNCTION()) const;

	void disconnect();
	void setState(State state);

	std::shared_ptr<PipelineHandler> pipe_;
	std::string id_;
	std::set<Stream *> streams_;
	std::unordered_map<const Stream *, StreamData> streamData_;
	std::list<PendingFence> pendingFences_;

	bool disconnected_;
	std::atomic<State> state_;

	std::unique_ptr<CameraControlValidator> validator_;

	friend PipelineHandler;
};

} /* namespace libcamera */
