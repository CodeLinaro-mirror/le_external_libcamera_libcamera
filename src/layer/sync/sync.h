/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * Copyright (C) 2025, Ideas On Board Oy
 *
 * Layer implementation for sync algorithm
 */

#pragma once

#include <arpa/inet.h>
#include <string>

#include <libcamera/base/unique_fd.h>
#include <libcamera/base/utils.h>

#include <libcamera/controls.h>
#include <libcamera/request.h>

#include "libcamera/internal/clock_recovery.h"

enum class Mode {
	Off,
	Server,
	Client,
};

struct SyncPayload {
	/* Frame duration in microseconds. */
	uint32_t frameDuration;
	/* Server wall clock version of the frame timestamp. */
	uint64_t wallClockFrameTimestamp;
	/* Server wall clock version of the sync time. */
	uint64_t wallClockReadyTime;
};

struct SyncLayerData {
	bool syncAvailable;

	/* Sync algorithm parameters */
	/* IP group address for sync messages */
	std::string group;
	/* port number for messages */
	uint16_t port;
	/* send a sync message every this many frames */
	uint32_t syncPeriod;
	/* don't adjust the client frame length by less than this (us) */
	uint32_t minAdjustment;
	/* This is the network interface to listen/multicast on */
	std::string networkInterface;

	/* Sync algorithm controls */
	Mode mode;
	libcamera::utils::Duration frameDuration;
	/* tell the application we're ready after this many frames */
	uint32_t readyFrame;

	/* Sync algorithm state */
	bool syncReady = false;
	unsigned int frameCount = 0;
	/* send the next packet when this reaches syncPeriod */
	uint32_t serverFrameCountPeriod = 0;
	/* the client's latest value for when the server will be "ready" */
	uint64_t serverReadyTime = 0;
	/* whether the client has received a packet yet */
	bool clientSeenPacket = false;

	/* estimate the true frame duration of the sensor (in us) */
	double frameDurationEstimated = 0;
	/* wall clock timestamp of previous frame (in us) */
	uint64_t lastWallClockFrameTimestamp;

	/* Frame length correction to apply */
	libcamera::utils::Duration frameDurationOffset;

	/* Infrastructure state */
	libcamera::ClockRecovery clockRecovery;
	sockaddr_in addr;
	libcamera::UniqueFD socket;
};

void *init(const std::string &id);
void terminate(void *closure);
libcamera::ControlList requestCompleted(void *closure, libcamera::Request *request);
libcamera::ControlInfoMap::Map updateControls(void *closure,
					      libcamera::ControlInfoMap &controls);
void queueRequest(void *closure, libcamera::Request *request);
void start(void *closure, libcamera::ControlList &controls);

void reset(SyncLayerData *data);
void initializeSocket(SyncLayerData *data);
void processControls(SyncLayerData *data, libcamera::ControlList &controls);
void processFrame(SyncLayerData *data, uint64_t frameWallClock,
		  libcamera::ControlList &metadata);
void processFrameServer(SyncLayerData *data, int64_t wallClockFrameTimestamp,
			libcamera::ControlList &metadata,
			unsigned int droppedFrames);
void processFrameClient(SyncLayerData *data, int64_t wallClockFrameTimestamp,
			libcamera::ControlList &metadata);
