/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * Copyright (C) 2026, Ideas on Board Oy
 *
 * V4L2 Event representations
 */

#include "libcamera/internal/v4l2_event.h"

#include <assert.h>
#include <stdint.h>

#include <linux/videodev2.h>

#include <libcamera/base/log.h>

/**
 * \file v4l2_event.h
 * \brief Handling of V4L2 Events
 */

namespace libcamera {

LOG_DEFINE_CATEGORY(V4L2Event)

/**
 * \class V4L2Event
 * \brief Base class to represent V4L2 Events
 *
 * The V4L2Event class holds common utilities and helper functions, and is
 * mostly intended to facilitate the creation of more specific derived classes.
 */

/**
 * \brief Construct a new V4L2Event::V4L2Event object
 *
 * \param[in] event The v4l2_event definition
 */
V4L2Event::V4L2Event(const v4l2_event *event)
{
	auto type = typeFromV4L2(event->type);
	assert(type);

	type_ = *type;
}

/**
 * \enum V4L2Event::Type
 * \brief Enumeration of possible V4L2 Event Types
 *
 * \var V4L2Event::VerticalSync
 * \brief An event triggered on the vertical sync
 *
 * \var V4L2Event::EndOfStream
 * \brief An event triggered when the end of a stream is reached
 *
 * \var V4L2Event::Control
 * \brief An event triggered when the value of the specified control changes,
 * if a button control is pressed or if the control's flags change.
 *
 * \var V4L2Event::FrameSync
 * \brief An event triggered when reception of a frame has begun
 *
 * \var V4L2Event::SourceChange
 * \brief Triggered when a source parameter is changed
 *
 * \var V4L2Event::MotionDetected
 * \brief Triggered when motion is detected
 *
 */

/**
 * \var V4L2Event::typeMap
 * \brief Maps V4L2 types from the kernel header to V4l2Event::Type values
 */

/**
 * \brief Create a derivation of a \ref V4L2Event from the kernel's
 * representation of one.
 *
 * This funtion allocates one of the derived classes of \ref V4L2Event, based on
 * the input event's type member.
 *
 * \param[in] event The kernel uAPI representation of a V4L2 event
 */
std::shared_ptr<V4L2Event> V4L2Event::createEvent(const struct v4l2_event *event)
{
	switch (event->type) {
	case V4L2_EVENT_VSYNC:
		return std::make_shared<V4L2VerticalSyncEvent>(event);
	case V4L2_EVENT_EOS:
		return std::make_shared<V4L2Event>(event);
	case V4L2_EVENT_CTRL:
		return std::make_shared<V4L2ControlEvent>(event);
	case V4L2_EVENT_FRAME_SYNC:
		return std::make_shared<V4L2FrameSyncEvent>(event);
	case V4L2_EVENT_SOURCE_CHANGE:
		return std::make_shared<V4L2SourceChangeEvent>(event);
	case V4L2_EVENT_MOTION_DET:
		return std::make_shared<V4L2MotionDetectedEvent>(event);
	default:
		return nullptr;
	}
}

/**
 * \fn V4L2Event::type()
 * \brief Return the \ref V4L2Event::Type associated with this event
 */

/**
 * \brief Map the kernel V4L2 event type to a \ref V4L2Event::Type
 *
 * \param[in] type The kernel's event type
 * \return std::optional<V4L2Event::Type>
 */
std::optional<V4L2Event::Type> V4L2Event::typeFromV4L2(uint32_t type)
{
	for (const auto &[v4l2Type, eventType] : typeMap) {
		if (v4l2Type == type)
			return eventType;
	}

	return std::nullopt;
}

/**
 * \brief Map a \ref V4L2Event::Type to the kernel's equivalent
 *
 * \param[in] type The V4L2Event::Type to translate
 * \return std::optional<uint32_t>
 */
std::optional<uint32_t> V4L2Event::typeToV4L2(V4L2Event::Type type)
{
	for (const auto &[v4l2Type, eventType] : typeMap) {
		if (eventType == type)
			return v4l2Type;
	}

	return std::nullopt;
}

/**
 * \class V4L2EventSubscription
 * \brief Class to represent the kernel uAPI's struct v4l2_event_subscription
 */

/**
 * \fn V4L2EventSubscription::V4L2EventSubscription
 * \brief Constructs a new V4L2EventSubscription object
 */

/**
 * \fn V4L2EventSubscription::type
 * \brief Returns the \ref V4L2Event::Type associated with this subscription
 */

/**
 * \fn V4L2EventSubscription::id
 * \brief Returns the ID field for this subscription
 */

/**
 * \fn V4L2EventSubscription::operator<(const V4L2EventSubscription &other) const
 * \brief "Less-than" comparator for event subscriptions
 *
 * This function allows us to check whether an equivalent subscription has
 * already been made by storing an instance of the class in a std::set. With
 * this operator the normal find() / insert() and erase() operations will
 * work on the set.
 */

/**
 * \class V4L2VerticalSyncEvent
 * \brief Representation of a V4L2Event of type VerticalSync
 *
 * \fn V4L2VerticalSyncEvent::V4L2VerticalSyncEvent
 * \brief Construct a new V4L2VerticalSyncEvent object
 *
 * \param[in] event The v4l2_event definition
 *
 * \fn V4L2VerticalSyncEvent::field
 * \brief The upcoming V4L2_FIELD_*
 */

/**
 * \class V4L2ControlEvent
 * \brief Representation of a V4L2Event of type Control
 *
 * \fn V4L2ControlEvent::controlId
 * \brief Return the V4L2_CID_* control ID associated with this event
 *
 * \fn V4L2ControlEvent::changes
 * \brief Return the bit mask of V4L2_EVENT_CTRL_CH_* flags
 *
 * \fn V4L2ControlEvent::type
 * \brief Return the V4L2_CTRL_TYPE_* for this control
 *
 * \fn V4L2ControlEvent::flags
 * \brief Return the V4L2_CTRL_FL_* bitmask for this control
 *
 * \fn V4L2ControlEvent::value
 * \brief Return the value of this control
 *
 * For the value, we take the value64 field from V4L2 and use it unconditionally
 *
 * \fn V4L2ControlEvent::min
 * \brief Return the minimum value of this control
 *
 * \fn V4L2ControlEvent::max
 * \brief Return the maximum value of this control
 *
 * \fn V4L2ControlEvent::step
 * \brief Return the control value step
 *
 * \fn V4L2ControlEvent::def
 * \brief Return the default value of this control
 */

/**
 * \brief Construct a new V4L2ControlEvent object
 *
 * \param[in] event The v4l2_event definition
 */
V4L2ControlEvent::V4L2ControlEvent(const struct v4l2_event *event)
	: V4L2Event(event), controlId_(event->id)
{
	const v4l2_event_ctrl *ctrl = &event->u.ctrl;

	changes_ = ctrl->changes;
	type_ = ctrl->type;
	flags_ = ctrl->flags;
	value_ = ctrl->value64;
	min_ = ctrl->minimum;
	max_ = ctrl->maximum;
	step_ = ctrl->step;
	default_ = ctrl->default_value;
}

/**
 * \class V4L2FrameSyncEvent
 * \brief Representation of a V4L2Event of type FrameSync
 *
 * \fn V4L2FrameSyncEvent::V4L2FrameSyncEvent()
 * \brief Construct a new V4L2FrameSyncEvent object
 *
 * \param[in] event The v4l2_event definition
 *
 * \fn V4L2FrameSyncEvent::sequence()
 * \brief Return the frame sequence number for this Frame Sync event
 */

/**
 * \class V4L2SourceChangeEvent
 * \brief Representation of a V4L2Event of type SourceChange
 *
 * \fn V4L2SourceChangeEvent::V4L2SourceChangeEvent()
 * \brief Construct a new V4L2SourceChangeEvent object
 *
 * \param[in] event The v4l2_event definition
 *
 * \fn V4L2SourceChangeEvent::changes()
 * \brief Return the change flags for this Source Change event
 */

/**
 * \class V4L2MotionDetectedEvent
 * \brief Representation of a V4L2Event of type MotionDetected
 *
 * \fn V4L2MotionDetectedEvent::V4L2MotionDetectedEvent()
 * \brief Construct a new V4L2MotionDetectedEvent object
 *
 * \param[in] event The v4l2_event definition
 *
 * \fn V4L2MotionDetectedEvent::flags
 * \brief Flags for this motion detection event, from V4L2_EVENT_MD_FL_*
 *
 * \fn V4L2MotionDetectedEvent::frameSequence
 * \brief The frame sequence number for this event
 *
 * This field is invalid unless \ref V4L2MotionDetectedEvent::flags includes
 * the V4L2_EVENT_MD_FL_HAVE_FRAME_SEQ flag.
 *
 * \fn  V4L2MotionDetectedEvent::regionMask
 * \brief The bitmask of the regions that reported motion
 */

} /* namespace libcamera */
