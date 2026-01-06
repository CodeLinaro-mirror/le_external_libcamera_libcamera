/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * Copyright (C) 2025, Ideas on Board Oy
 */

#pragma once

#include <libcamera/controls.h>

namespace libcamera::controls::details {

struct TypeInfo {
	std::size_t size = 0;
	std::size_t alignment = 0;

	explicit operator bool() const { return alignment != 0; }

	static constexpr TypeInfo get(ControlType t)
	{
		switch (t) {
		case ControlTypeNone: return {};
		case ControlTypeBool: return { sizeof(bool), alignof(bool) };
		case ControlTypeByte: return { sizeof(uint8_t), alignof(uint8_t) };
		case ControlTypeUnsigned16: return { sizeof(uint16_t), alignof(uint16_t) };
		case ControlTypeUnsigned32: return { sizeof(uint32_t), alignof(uint32_t) };
		case ControlTypeInteger32: return { sizeof(int32_t), alignof(int32_t) };
		case ControlTypeInteger64: return { sizeof(int64_t), alignof(int64_t) };
		case ControlTypeFloat: return { sizeof(float), alignof(float) };
		case ControlTypeString: return { sizeof(char), alignof(char) };
		case ControlTypeRectangle: return { sizeof(Rectangle), alignof(Rectangle) };
		case ControlTypeSize: return { sizeof(Size), alignof(Size) };
		case ControlTypePoint: return { sizeof(Point), alignof(Point) };
		}

		return {};
	}
};

} /* libcamera::controls::details */
