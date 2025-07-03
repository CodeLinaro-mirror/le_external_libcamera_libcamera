/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * Copyright (C) 2025, Ideas On Board Oy
 *
 * Layer manager interface
 */

#pragma once

#include <deque>
#include <dlfcn.h>
#include <map>
#include <set>
#include <string>
#include <tuple>

#include <libcamera/base/log.h>
#include <libcamera/base/span.h>

#include <libcamera/camera.h>
#include <libcamera/controls.h>
#include <libcamera/framebuffer.h>
#include <libcamera/layer.h>
#include <libcamera/request.h>
#include <libcamera/stream.h>

namespace libcamera {

LOG_DECLARE_CATEGORY(LayerManager)

class LayerManager
{
public:
	LayerManager();
	~LayerManager() = default;

	void init(const Camera *camera, const ControlList &properties,
		  const ControlInfoMap &controlInfoMap);
	void terminate(const Camera *camera);

	void bufferCompleted(const Camera *camera,
			     Request *request, FrameBuffer *buffer);
	void requestCompleted(const Camera *camera, Request *request);
	void disconnected(const Camera *camera);

	void acquire(const Camera *camera);
	void release(const Camera *camera);

	const ControlInfoMap &controls(const Camera *camera) const { return controls_.at(camera); }
	const ControlList &properties(const Camera *camera) const { return properties_.at(camera); }

	void configure(const Camera *camera, const CameraConfiguration *config,
		       const ControlInfoMap &controlInfoMap);

	void createRequest(const Camera *camera,
			   uint64_t cookie, const Request *request);

	void queueRequest(const Camera *camera, Request *request);

	void start(const Camera *camera, const ControlList *controls);
	void stop(const Camera *camera);

private:
	/* Extend the layer with information specific to load-handling */
	struct LayerLoaded
	{
		LayerLoaded()
			: info(nullptr), vtable(nullptr), dlHandle(nullptr)
		{
		}

		LayerLoaded(LayerLoaded &&other)
			: info(other.info), vtable(other.vtable),
			  dlHandle(other.dlHandle)
		{
			other.dlHandle = nullptr;
		}

		LayerLoaded &operator=(LayerLoaded &&other)
		{
			info = other.info;
			vtable = other.vtable;
			dlHandle = other.dlHandle;
			other.dlHandle = nullptr;
			return *this;
		}

		~LayerLoaded()
		{
			if (dlHandle)
				dlclose(dlHandle);
		}

		LayerInfo *info;
		LayerInterface *vtable;
		void *dlHandle;

	private:
		LIBCAMERA_DISABLE_COPY(LayerLoaded)
	};

	using ClosureKey = std::tuple<const Camera *, const LayerLoaded *>;

	void updateProperties(const Camera *camera,
			      const ControlList &properties);
	void updateControls(const Camera *camera,
			    const ControlInfoMap &controlInfoMap);

	LayerLoaded createLayer(const std::string &file);
	std::deque<LayerLoaded> executionQueue_;
	std::map<ClosureKey, void *> closures_;

	std::map<const Camera *, ControlInfoMap> controls_;
	std::map<const Camera *, ControlList> properties_;
};

} /* namespace libcamera */
