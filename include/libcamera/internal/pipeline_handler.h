/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * Copyright (C) 2018, Google Inc.
 *
 * Pipeline handler infrastructure
 */

#pragma once

#include <functional>
#include <memory>
#include <string>
#include <sys/types.h>
#include <vector>

#include <libcamera/base/internal/cxx20.h>
#include <libcamera/base/object.h>

#include <libcamera/camera.h>
#include <libcamera/controls.h>
#include <libcamera/stream.h>

#include "libcamera/internal/request.h"

namespace libcamera {

class Camera;
class CameraConfiguration;
class CameraManager;
class DeviceEnumerator;
class DeviceMatch;
class FrameBuffer;
class MediaDevice;
class PipelineHandler;
class Request;

class PipelineHandler : public std::enable_shared_from_this<PipelineHandler>,
			public Object
{
public:
	PipelineHandler(CameraManager *manager,
			unsigned int maxQueuedRequestsDevice = 32);
	virtual ~PipelineHandler();

	virtual bool match(DeviceEnumerator *enumerator) = 0;
	MediaDevice *acquireMediaDevice(DeviceEnumerator *enumerator,
					const DeviceMatch &dm);

	bool acquire(Camera *camera);
	void release(Camera *camera);

	virtual std::unique_ptr<CameraConfiguration> generateConfiguration(Camera *camera,
									   Span<const StreamRole> roles) = 0;
	virtual int configure(Camera *camera, CameraConfiguration *config) = 0;

	virtual int exportFrameBuffers(Camera *camera, Stream *stream,
				       std::vector<std::unique_ptr<FrameBuffer>> *buffers) = 0;

	virtual int start(Camera *camera, const ControlList *controls) = 0;
	void stop(Camera *camera);
	bool hasPendingRequests(const Camera *camera) const;

	void registerRequest(Request *request);
	void queueRequest(Request *request);

	void metadataAvailable(Request *request, const ControlList &metadata);

	template<typename T>
	void metadataAvailable(Request *request, const Control<T> &ctrl,
			       const internal::cxx20::type_identity_t<T> &value)
	{
		auto &m = request->metadata();
		const auto c = m.checkpoint();

		std::ignore = m.set(ctrl, value);
		request->metadata2().set(ctrl, value);

		const auto d = c.diffSince();
		if (d)
			request->_d()->camera()->metadataAvailable.emit(request, d);
	}

#ifndef __DOXYGEN__
	struct MetadataSetter {
		Request *request;

		template<typename T>
		void operator()(const Control<T> &ctrl,
			        const internal::cxx20::type_identity_t<T> &value) const
		{
			std::ignore = request->metadata().set(ctrl, value);
			request->metadata2().set(ctrl, value);
		}
	};

	template<typename Func, std::enable_if_t<std::is_invocable_v<Func&, MetadataSetter>> * = nullptr>
#else
	template<typename Func>
#endif
	void metadataAvailable(Request *request, Func func)
	{
		const auto c = request->metadata().checkpoint();

		std::invoke(func, MetadataSetter{ request });

		if (const auto d = c.diffSince())
			request->_d()->camera()->metadataAvailable.emit(request, d);
	}

	bool completeBuffer(Request *request, FrameBuffer *buffer);
	void completeRequest(Request *request);
	void cancelRequest(Request *request);

	std::string configurationFile(const std::string &subdir,
				      const std::string &name,
				      bool silent = false) const;

	const char *name() const { return name_; }

	CameraManager *cameraManager() const { return manager_; }

protected:
	void registerCamera(std::shared_ptr<Camera> camera);
	void hotplugMediaDevice(MediaDevice *media);

	virtual int queueRequestDevice(Camera *camera, Request *request) = 0;
	virtual void stopDevice(Camera *camera) = 0;

	virtual bool acquireDevice(Camera *camera);
	virtual void releaseDevice(Camera *camera);

	CameraManager *manager_;
	const unsigned int maxQueuedRequestsDevice_;

private:
	void unlockMediaDevices();

	void mediaDeviceDisconnected(MediaDevice *media);
	virtual void disconnect();

	void doQueueRequest(Request *request);
	void doQueueRequests(Camera *camera);

	std::vector<std::shared_ptr<MediaDevice>> mediaDevices_;
	std::vector<std::weak_ptr<Camera>> cameras_;

	const char *name_;
	unsigned int useCount_;

	friend class PipelineHandlerFactoryBase;
};

class PipelineHandlerFactoryBase
{
public:
	PipelineHandlerFactoryBase(const char *name);
	virtual ~PipelineHandlerFactoryBase() = default;

	std::shared_ptr<PipelineHandler> create(CameraManager *manager) const;

	const std::string &name() const { return name_; }

	static std::vector<PipelineHandlerFactoryBase *> &factories();
	static const PipelineHandlerFactoryBase *getFactoryByName(const std::string &name);

private:
	static void registerType(PipelineHandlerFactoryBase *factory);

	virtual std::unique_ptr<PipelineHandler>
	createInstance(CameraManager *manager) const = 0;

	std::string name_;
};

template<typename _PipelineHandler>
class PipelineHandlerFactory final : public PipelineHandlerFactoryBase
{
public:
	PipelineHandlerFactory(const char *name)
		: PipelineHandlerFactoryBase(name)
	{
	}

	std::unique_ptr<PipelineHandler>
	createInstance(CameraManager *manager) const override
	{
		return std::make_unique<_PipelineHandler>(manager);
	}
};

#define REGISTER_PIPELINE_HANDLER(handler, name) \
	static PipelineHandlerFactory<handler> global_##handler##Factory(name);

} /* namespace libcamera */
