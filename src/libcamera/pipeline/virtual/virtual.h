/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * Copyright (C) 2023, Google Inc.
 *
 * virtual.h - Pipeline handler for virtual cameras
 */

#pragma once

#include <libcamera/base/file.h>

#include "libcamera/internal/camera.h"
#include "libcamera/internal/dma_buf_allocator.h"
#include "libcamera/internal/pipeline_handler.h"

namespace libcamera {

class VirtualCameraData : public Camera::Private
{
public:
	struct Resolution {
		Size size;
		std::vector<int> frame_rates;
	};
	VirtualCameraData(PipelineHandler *pipe)
		: Camera::Private(pipe)
	{
	}

	~VirtualCameraData() = default;

	std::vector<Resolution> supportedResolutions_;

	Stream stream_;
};

class VirtualCameraConfiguration : public CameraConfiguration
{
public:
	static constexpr unsigned int kBufferCount = 4;

	VirtualCameraConfiguration(VirtualCameraData *data);

	Status validate() override;

private:
	const VirtualCameraData *data_;
};

class PipelineHandlerVirtual : public PipelineHandler
{
public:
	PipelineHandlerVirtual(CameraManager *manager);

	std::unique_ptr<CameraConfiguration> generateConfiguration(Camera *camera,
								   Span<const StreamRole> roles) override;
	int configure(Camera *camera, CameraConfiguration *config) override;

	int exportFrameBuffers(Camera *camera, Stream *stream,
			       std::vector<std::unique_ptr<FrameBuffer>> *buffers) override;

	int start(Camera *camera, const ControlList *controls) override;
	void stopDevice(Camera *camera) override;

	int queueRequestDevice(Camera *camera, Request *request) override;

	bool match(DeviceEnumerator *enumerator) override;

private:
	VirtualCameraData *cameraData(Camera *camera)
	{
		return static_cast<VirtualCameraData *>(camera->_d());
	}

	DmaBufAllocator dmaBufAllocator_;
};

} // namespace libcamera
