/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * Copyright (C) 2024, Ideas on Board Oy
 *
 * time_sheet.h
 */

#pragma once

#include <memory>
#include <ostream>
#include <vector>

#include <libcamera/libcamera.h>

class TimeSheetEntry
{
public:
	TimeSheetEntry() = delete;
	TimeSheetEntry(const libcamera::ControlIdMap &idmap);
	TimeSheetEntry(TimeSheetEntry &&other) = default;
	TimeSheetEntry(const TimeSheetEntry &) = delete;
	~TimeSheetEntry() = default;

	libcamera::ControlList &controls() { return controls_; }
	const libcamera::ControlList &metadata() const { return metadata_; }
	void handleCompleteRequest(libcamera::Request *request,
				   const TimeSheetEntry *previous);
	double spotBrightness() const { return spotBrightness_; }
	double brightnessChange() const { return brightnessChange_; }

	friend std::ostream &operator<<(std::ostream &os, const TimeSheetEntry &te);

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
		: idmap_(idmap), entries_(count)
	{
	}

	void prepareForQueue(libcamera::Request *request, uint32_t sequence);
	void handleCompleteRequest(libcamera::Request *request);

	TimeSheetEntry &operator[](size_t pos) { return get(pos); }
	TimeSheetEntry &get(size_t pos);
	size_t size() const { return entries_.size(); }

	friend std::ostream &operator<<(std::ostream &os, const TimeSheet &ts);

private:
	const libcamera::ControlIdMap &idmap_;
	std::vector<std::unique_ptr<TimeSheetEntry>> entries_;
};
