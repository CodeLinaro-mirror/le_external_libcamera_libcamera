/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * Copyright (C) 2026, Raspberry Pi Ltd
 *
 */

#pragma once

#include <memory>
#include <string>
#include <vector>

#include <libcamera/base/class.h>

#include <libcamera/camera_descriptor.h>
#include <libcamera/controls.h>

namespace libcamera {

class MediaDevice;
class PipelineHandlerFactoryBase;

class CameraDescriptor::Private : public Extensible::Private
{
	LIBCAMERA_DECLARE_PUBLIC(CameraDescriptor)

public:
	Private();

	const PipelineHandlerFactoryBase *factory_;
	std::vector<std::shared_ptr<MediaDevice>> mediaDevices_;
	std::string entityName_;
	std::string id_;
	ControlList properties_;
};

} /* namespace libcamera */
