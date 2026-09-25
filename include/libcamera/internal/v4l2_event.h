/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * Copyright (C) 2026, Ideas on Board Oy
 *
 * V4L2 Event representations
 */

#pragma once

#include <array>
#include <memory>
#include <optional>
#include <stdint.h>
#include <utility>

#include <linux/videodev2.h>

namespace libcamera {

class V4L2Event
{
public:
	V4L2Event(const v4l2_event *event);

	enum Type {
		VerticalSync,
		EndOfStream,
		Control,
		FrameSync,
		SourceChange,
		MotionDetected,
		NumberOfEventTypes,
	};

	static constexpr std::array<std::pair<uint32_t, Type>, Type::NumberOfEventTypes> typeMap = { {
		{ V4L2_EVENT_VSYNC, Type::VerticalSync },
		{ V4L2_EVENT_EOS, Type::EndOfStream },
		{ V4L2_EVENT_CTRL, Type::Control },
		{ V4L2_EVENT_FRAME_SYNC, Type::FrameSync },
		{ V4L2_EVENT_SOURCE_CHANGE, Type::SourceChange },
		{ V4L2_EVENT_MOTION_DET, Type::MotionDetected },
	} };

	static std::shared_ptr<V4L2Event> createEvent(const struct v4l2_event *event);
	Type type() { return type_; }
	static std::optional<Type> typeFromV4L2(uint32_t type);
	static std::optional<uint32_t> typeToV4L2(Type type);

private:
	Type type_;
};

class V4L2EventSubscription
{
public:
	V4L2EventSubscription(V4L2Event::Type type, uint32_t id = 0)
		: type_(type), id_(id)
	{
	}

	V4L2Event::Type type() { return type_; }
	uint32_t id() { return id_; }

	bool operator<(const V4L2EventSubscription &other) const
	{
		return std::tie(type_, id_) < std::tie(other.type_, other.id_);
	}
private:
	V4L2Event::Type type_;
	uint32_t id_;
};

class V4L2VerticalSyncEvent : public V4L2Event
{
public:
	V4L2VerticalSyncEvent(const v4l2_event *event)
		: V4L2Event(event), field_(event->u.vsync.field)
	{
	}

	uint8_t field() { return field_; }
private:
	uint8_t field_;
};

class V4L2ControlEvent : public V4L2Event
{
public:
	V4L2ControlEvent(const v4l2_event *event);

	uint32_t controlId() { return controlId_; }
	uint32_t changes() { return changes_; }
	uint32_t type() { return type_; }
	uint32_t flags() { return flags_; }
	int64_t value() { return value_; }
	uint32_t min() { return min_; }
	uint32_t max() { return max_; }
	uint32_t step() { return step_; }
	uint32_t def() { return default_; }
private:
	uint32_t controlId_;
	uint32_t changes_;
	uint32_t type_;
	uint32_t flags_;
	int64_t value_;
	uint32_t min_;
	uint32_t max_;
	uint32_t step_;
	uint32_t default_;
};

class V4L2FrameSyncEvent : public V4L2Event
{
public:
	V4L2FrameSyncEvent(const v4l2_event *event)
		: V4L2Event(event), sequence_(event->u.frame_sync.frame_sequence)
	{
	}

	uint32_t sequence() { return sequence_; }
private:
	uint32_t sequence_;
};

class V4L2SourceChangeEvent : public V4L2Event
{
public:
	V4L2SourceChangeEvent(const v4l2_event *event)
		: V4L2Event(event), changes_(event->u.src_change.changes)
	{
	}

	uint32_t changes() { return changes_; }

private:
	uint32_t changes_;
};

class V4L2MotionDetectedEvent : public V4L2Event
{
public:
	V4L2MotionDetectedEvent(const v4l2_event *event)
		: V4L2Event(event), flags_(event->u.motion_det.flags),
		  frame_sequence_(event->u.motion_det.frame_sequence),
		  region_mask_(event->u.motion_det.region_mask)
	{
	}

	uint32_t flags() { return flags_; }
	uint32_t frameSequence() { return frame_sequence_; }
	uint32_t regionMask() { return region_mask_; }
private:
	uint32_t flags_;
	uint32_t frame_sequence_;
	uint32_t region_mask_;
};

} /* namespace libcamera */
