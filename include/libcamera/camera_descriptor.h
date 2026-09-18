/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * Copyright (C) 2026, Raspberry Pi Ltd
 *
 */

#pragma once

#include <memory>
#include <string>

#include <libcamera/base/class.h>

#include <libcamera/controls.h>

namespace libcamera {

class CameraDescriptor final : public Extensible
{
	LIBCAMERA_DECLARE_PRIVATE()

public:
	static std::shared_ptr<CameraDescriptor> create(std::unique_ptr<Private> d);

	const std::string &id() const;
	const ControlList &properties() const;

private:
	LIBCAMERA_DISABLE_COPY(CameraDescriptor)

	CameraDescriptor(std::unique_ptr<Private> d);
};

} /* namespace libcamera */
