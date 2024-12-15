/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * Copyright (C) 2018, Google Inc.
 *
 * API to enumerate and find media devices
 */

#pragma once

#include <memory>
#include <string>
#include <vector>

#include <libcamera/base/signal.h>

namespace libcamera {

class MediaDevice;

class DeviceMatch
{
public:
	DeviceMatch(std::string_view driver);

	void add(std::string_view entity);

	bool match(const MediaDevice *device) const;

private:
	std::string driver_;
	std::vector<std::string> entities_;
};

class DeviceEnumerator
{
public:
	static std::unique_ptr<DeviceEnumerator> create();

	virtual ~DeviceEnumerator();

	virtual int init() = 0;
	virtual int enumerate() = 0;

	std::shared_ptr<MediaDevice> search(const DeviceMatch &dm);

	Signal<> devicesAdded;

protected:
	std::unique_ptr<MediaDevice> createDevice(std::string_view deviceNode);
	void addDevice(std::unique_ptr<MediaDevice> media);
	void removeDevice(std::string_view deviceNode);

private:
	std::vector<std::shared_ptr<MediaDevice>> devices_;
};

} /* namespace libcamera */
