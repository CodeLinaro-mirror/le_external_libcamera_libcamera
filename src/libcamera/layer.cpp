/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * Copyright (C) 2025, Ideas On Board Oy
 *
 * Layer interface
 */

#include <libcamera/layer.h>

/**
 * \file layer.h
 * \brief Layer interface
 *
 * Layers are a construct that lives in between the application and libcamera.
 * They are hooked into select calls to and from Camera, and each one is
 * executed in order.
 *
 * \todo Expand on this, and decide on more concrete naming like module vs implementation
 */

namespace libcamera {

/**
 * \struct LayerInfo
 * \brief Information about a Layer implementation
 *
 * This struct gives information about a layer implementation, such as name and
 * API version. It must be exposed, named 'layerInfo', by the layer
 * implementation shared object to identify itself to the LayerManager.
 */

/**
 * \var LayerInfo::name
 * \brief Name of the Layer module
 */

/**
 * \var LayerInfo::layerAPIVersion
 * \brief API version of the Layer implementation
 */

/**
 * \struct LayerInterface
 * \brief The function table of the Layer implementation
 *
 * This struct is the function table of a layer implementation. Any functions
 * that the layer implements should be filled in here, and any functions that
 * are not implemented must be set to nullptr. This struct, named
 * 'layerInterface', must be exposed by the layer implementation shared object.
 */

/**
 * \var LayerInterface::init
 * \brief Initialize the layer
 * \param[in] name Name of the camera
 *
 * This function is a required function for layer implementations.
 *
 * This function is called on Camera construction, and is where the layer
 * implementation should allocated and initialize its closure and anything else
 * required to run.
 *
 * \return A closure
 */

/**
 * \var LayerInterface::terminate
 * \brief Terminate the layer
 * \param[in] closure The closure that was allocated at init()
 *
 * This function is a required function for layer implementations.
 *
 * This function is called on Camera deconstruction, and is where the layer
 * should free its closure and anything else that was allocated at
 * initialization.
 */

/**
 * \var LayerInterface::bufferCompleted
 * \brief Hook for Camera::bufferCompleted
 * \param[in] closure The closure of the layer
 */

/**
 * \var LayerInterface::requestCompleted
 * \brief Hook for Camera::requestCompleted
 * \param[in] closure The closure of the layer
 */

/**
 * \var LayerInterface::disconnected
 * \brief Hook for Camera::disconnected
 * \param[in] closure The closure of the layer
 */

/**
 * \var LayerInterface::acquire
 * \brief Hook for Camera::acquire
 * \param[in] closure The closure of the layer
 */

/**
 * \var LayerInterface::release
 * \brief Hook for Camera::release
 * \param[in] closure The closure of the layer
 */

/**
 * \var LayerInterface::controls
 * \brief Declare the controls supported by the Layer
 * \param[in] closure The closure of the layer
 * \param[in] controlInfoMap The cumulative ControlInfoMap of supported controls of the Camera and any previous layers
 *
 * This function is for the layer implementation to declare the controls that
 * it supports. This will be called by the LayerManager at Camera::init() time
 * (after LayerInterface::init()), and at Camera::configure() time. The latter
 * gives a chance for the controls to be updated if the configuration changes
 * them.
 *
 * The controls that are returned by this function will overwrite any
 * duplicates that were in the input parameter controls.
 *
 * \return The additional controls that this Layer implements
 */

/**
 * \var LayerInterface::properties
 * \brief Declare the properties supported by the Layer
 * \param[in] closure The closure of the layer
 * \param[in] controlList The cumulative properties of the Camera and any previous layers
 *
 * This function is for the layer implementation to declare the properies that
 * it wants to declare. This will be called by the LayerManager once at
 * Camera::init() time (after LayerInterface::init(), and before
 * LayerInterface::controls()).
 *
 * The properties that are returned by this function will overwrite any
 * duplicates that were in the input parameter properties.
 *
 * \return The additional properties that this Layer declares
 */

/**
 * \var LayerInterface::configure
 * \brief Hook for Camera::configure
 * \param[in] closure The closure of the layer
 * \param[in] cameraConfiguration The camera configuration
 */

/**
 * \var LayerInterface::createRequest
 * \brief Hook for Camera::createRequest
 * \param[in] closure The closure of the layer
 * \param[in] cookie An opaque cookie for the application
 * \param[in] request The request that was just created by the Camera
 */

/**
 * \var LayerInterface::queueRequest
 * \brief Hook for Camera::queueRequest
 * \param[in] closure The closure of the layer
 * \param[in] request The request that was queued
 */

/**
 * \var LayerInterface::start
 * \brief Hook for Camera::start
 * \param[in] closure The closure of the layer
 * \param[in] controls The controls to be set before starting capture
 */

/**
 * \var LayerInterface::stop
 * \brief Hook for Camera::stop
 * \param[in] closure The closure of the layer
 */

} /* namespace libcamera */
