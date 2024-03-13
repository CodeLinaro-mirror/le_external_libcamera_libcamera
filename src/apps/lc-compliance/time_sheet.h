/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * Copyright (C) 2024, Ideas on Board Oy
 *
 * time_sheet.h
 */

#pragma once

#include <future>
#include <vector>

#include <libcamera/libcamera.h>

class TimeSheetEntry
{
public:
	TimeSheetEntry() = delete;
	TimeSheetEntry(const libcamera::ControlIdMap &idmap);
	TimeSheetEntry(TimeSheetEntry &&other) noexcept = default;
	TimeSheetEntry(const TimeSheetEntry &) = delete;

	libcamera::ControlList &controls() { return controls_; };
	libcamera::ControlList &metadata() { return metadata_; };
	void handleCompleteRequest(libcamera::Request *request, const TimeSheetEntry *previous);
	void printInfo();
	double getSpotBrightness() const { return spotBrightness_; };
	double getBrightnessChange() const { return brightnessChange_; };

private:
	double spotBrightness_ = 0.0;
	double brightnessChange_ = 0.0;
	libcamera::ControlList controls_;
	libcamera::ControlList metadata_;
	uint32_t sequence_ = 0;
};

class TimeSheet
{
public:
	TimeSheet(int count, const libcamera::ControlIdMap &idmap)
		: idmap_(idmap), entries_(count){};

	void prepareForQueue(libcamera::Request *request, uint32_t sequence);
	void handleCompleteRequest(libcamera::Request *request);
	void printAllInfos();

	TimeSheetEntry &operator[](size_t pos) { return get(pos); };
	TimeSheetEntry &get(size_t pos);
	size_t size() const { return entries_.size(); };

private:
	const libcamera::ControlIdMap &idmap_;
	std::vector<std::shared_ptr<TimeSheetEntry>> entries_;
};
