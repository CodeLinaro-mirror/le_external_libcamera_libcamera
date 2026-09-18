/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * Copyright (C) 2026, Raspberry Pi Ltd
 *
 */

#include "libcamera/internal/camera_descriptor.h"

#include <memory>

#include <libcamera/camera_descriptor.h>
#include <libcamera/property_ids.h>

/**
 * \file libcamera/camera_descriptor.h
 * \brief Describing cameras ahead of initialisation
 */

namespace libcamera {

/**
 * \class CameraDescriptor
 * \brief Describe a camera known to the system but not yet initialised
 *
 * A CameraDescriptor represents a camera discovered during device enumeration,
 * before any camera initialisation has taken place. Descriptors are produced by
 * CameraManager::enumerate() without acquiring any device, and carry the
 * information about a camera that is available at enumeration time.
 *
 * The descriptor holds the camera identifier, guaranteed to be identical to
 * the Camera::id() of the corresponding Camera instance, and a list of
 * properties known at enumeration time. A descriptor can be passed to
 * CameraManager::initialize() to create the corresponding fully initialised
 * Camera.
 */

#ifndef __DOXYGEN_PUBLIC__
/**
 * \class CameraDescriptor::Private
 * \brief Base class for camera descriptor private data
 */

/**
 * \brief Construct a CameraDescriptor::Private instance
 */
CameraDescriptor::Private::Private()
	: factory_(nullptr), properties_(properties::properties)
{
}

/**
 * \var CameraDescriptor::Private::factory_
 * \brief The factory of the pipeline handler that produced this descriptor
 *
 * This is set by the camera manager when it collects the descriptors reported
 * by a pipeline handler, and is used to create a pipeline handler for the
 * camera when the descriptor is initialised.
 */

/**
 * \var CameraDescriptor::Private::mediaDevices_
 * \brief The media devices the camera is part of
 *
 * The media devices needed to initialise the camera. The first entry is the
 * media device that identifies the camera's pipeline instance, and is used to
 * route the camera to a live pipeline handler holding it.
 */

/**
 * \var CameraDescriptor::Private::entityName_
 * \brief The name of the camera's main media entity
 *
 * The entity that identifies the camera within its media device, for instance
 * the camera sensor for a CSI receiver or the default video node for a USB
 * camera.
 */

/**
 * \var CameraDescriptor::Private::id_
 * \brief The camera identifier
 * \sa CameraDescriptor::id()
 */

/**
 * \var CameraDescriptor::Private::properties_
 * \brief The properties of the camera known at enumeration time
 * \sa CameraDescriptor::properties()
 */
#endif /* __DOXYGEN_PUBLIC__ */

/**
 * \brief Create a camera descriptor instance
 * \param[in] d Camera descriptor private data
 *
 * The caller is responsible for populating the private data before creating
 * the descriptor.
 *
 * \return A shared pointer to the newly created camera descriptor object
 */
std::shared_ptr<CameraDescriptor>
CameraDescriptor::create(std::unique_ptr<Private> d)
{
	return std::shared_ptr<CameraDescriptor>(new CameraDescriptor(std::move(d)));
}

/**
 * \brief Retrieve the ID of the camera
 *
 * The camera ID is identical to the Camera::id() of the Camera instance
 * created by initialising this descriptor. It is guaranteed to be unique and
 * stable so the same camera will have the same ID across both unplug/replug and
 * boot cycles.
 *
 * \return ID of the camera
 */
const std::string &CameraDescriptor::id() const
{
	return _d()->id_;
}

/**
 * \brief Retrieve the properties of the camera known at enumeration time
 *
 * Camera properties are metadata that describe the camera. Only the subset of
 * properties that can be determined at enumeration time (without initialising
 * the camera) is reported here. The complete property list is available from
 * Camera::properties() once the camera has been initialised.
 *
 * \return The list of camera properties known at enumeration time
 */
const ControlList &CameraDescriptor::properties() const
{
	return _d()->properties_;
}

CameraDescriptor::CameraDescriptor(std::unique_ptr<Private> d)
	: Extensible(std::move(d))
{
}

} /* namespace libcamera */
