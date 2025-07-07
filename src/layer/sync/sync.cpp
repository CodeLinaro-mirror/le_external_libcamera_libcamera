/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * Copyright (C) 2024, Raspberry Pi Ltd
 * Copyright (C) 2025, Ideas On Board Oy
 *
 * Layer implementation for sync algorithm
 */

#include "sync.h"

#include <arpa/inet.h>
#include <chrono>
#include <fcntl.h>
#include <netinet/ip.h>
#include <string.h>
#include <string_view>
#include <sys/socket.h>
#include <unistd.h>

#include <libcamera/base/log.h>
#include <libcamera/base/unique_fd.h>
#include <libcamera/base/utils.h>

#include <libcamera/control_ids.h>
#include <libcamera/layer.h>

namespace libcamera {

LOG_DEFINE_CATEGORY(SyncLayer)

} /* namespace libcamera */

using namespace libcamera;
using namespace std::chrono_literals;

void *init([[maybe_unused]] const std::string &id)
{
	SyncLayerData *data = new SyncLayerData;

	LOG(SyncLayer, Info) << "Initializing sync layer";

	const char *kDefaultGroup = "239.255.255.250";
	constexpr unsigned int kDefaultPort = 10000;
	constexpr unsigned int kDefaultSyncPeriod = 30;
	constexpr unsigned int kDefaultReadyFrame = 100;
	constexpr unsigned int kDefaultMinAdjustment = 50;

	/* \todo load these from configuration file */
	data->group = kDefaultGroup;
	data->port = kDefaultPort;
	data->syncPeriod = kDefaultSyncPeriod;
	data->readyFrame = kDefaultReadyFrame;
	data->minAdjustment = kDefaultMinAdjustment;

	return data;
}

void terminate(void *closure)
{
	SyncLayerData *data = static_cast<SyncLayerData *>(closure);

	data->socket.reset();

	delete data;
}

ControlList requestCompleted(void *closure, Request *request)
{
	SyncLayerData *data = static_cast<SyncLayerData *>(closure);

	const ControlList &metadata = request->metadata();
	ControlList ret;

	/* SensorTimestamp is required for sync */
	/* \todo Document this requirement (along with SyncAdjustment) */
	auto sensorTimestamp = metadata.get<int64_t>(controls::SensorTimestamp);
	if (sensorTimestamp) {
		data->clockRecovery.addSample();
		uint64_t frameWallClock = data->clockRecovery.getOutput(*sensorTimestamp);
		ret.set(controls::FrameWallClock, static_cast<int64_t>(frameWallClock));
		if (data->mode != Mode::Off && data->frameDuration)
			processFrame(data, frameWallClock, ret);
	}

	return ret;
}

ControlInfoMap::Map updateControls(void *closure, ControlInfoMap &controls)
{
	SyncLayerData *data = static_cast<SyncLayerData *>(closure);

	/*
	 * If the SyncAdjustment control is unavailable then the Camera does
	 * not support Sync adjustment
	 */
	auto it = controls.find(&controls::SyncAdjustment);
	data->syncAvailable = it != controls.end();
	if (!data->syncAvailable) {
		LOG(SyncLayer, Warning)
			<< "Sync layer is not supported: SyncAdjustment control is not available";
		return {};
	}

	/*
	 * Save the default FrameDurationLimits. If it's not available then we
	 * have to wait until one is supplied in a request. We cannot use the
	 * FrameDuration returned from the first frame as the
	 * FrameDurationLimits has to be explicitly set by the application, as
	 * this is the frame rate target that the cameras will sync to.
	 * \todo Document that FrameDurationLimits is a required control
	 */
	it = controls.find(&controls::FrameDurationLimits);
	if (it != controls.end())
		data->frameDuration = std::chrono::microseconds(it->second.min().get<int64_t>());

	return {
		{ &controls::rpi::SyncMode,
		  ControlInfo(controls::rpi::SyncModeValues) },
		{ &controls::rpi::SyncFrames,
		  ControlInfo(1, 1000000, 100) },
		{ &controls::SyncInterface,
		  ControlInfo(1, 40) }
	};
}

void queueRequest(void *closure, Request *request)
{
	SyncLayerData *data = static_cast<SyncLayerData *>(closure);
	if (!data->syncAvailable)
		return;

	processControls(data, request->controls());

	if (data->mode == Mode::Client) {
		request->controls().set(controls::SyncAdjustment,
					data->frameDurationOffset.count() / 1000);
		data->frameDurationOffset = utils::Duration(0);
	}
}

void start(void *closure, ControlList &controls)
{
	SyncLayerData *data = static_cast<SyncLayerData *>(closure);
	if (!data->syncAvailable)
		return;

	reset(data);
	data->clockRecovery.addSample();
	processControls(data, controls);
}

void reset(SyncLayerData *data)
{
	data->syncReady = false;
	data->frameCount = 0;
	data->serverFrameCountPeriod = 0;
	data->serverReadyTime = 0;
	data->clientSeenPacket = false;
}

void initializeSocket(SyncLayerData *data)
{
	ssize_t ret;
	struct ip_mreq mreq{};
	socklen_t addrlen;
	SyncPayload payload;
	unsigned int en = 1;

	if (data->socket.isValid()) {
		LOG(SyncLayer, Debug)
			<< "Socket already exists; not recreating";
		return;
	}

	if (!data->networkInterface.empty())
		mreq.imr_interface.s_addr = inet_addr(data->networkInterface.c_str());
	else
		mreq.imr_interface.s_addr = htonl(INADDR_ANY);

	int flags = SOCK_CLOEXEC;
	if (data->mode == Mode::Client)
		flags |= SOCK_NONBLOCK;

	int fd = socket(AF_INET, SOCK_DGRAM | flags, 0);
	if (fd < 0) {
		LOG(SyncLayer, Error) << "Unable to create socket";
		return;
	}
	data->socket = UniqueFD(fd);

	struct sockaddr_in &addr = data->addr;
	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = data->mode == Mode::Client ? htonl(INADDR_ANY) : inet_addr(data->group.c_str());
	addr.sin_port = htons(data->port);

	if (data->mode != Mode::Client)
		return;

	if (setsockopt(data->socket.get(), SOL_SOCKET, SO_REUSEADDR, &en, sizeof(en)) < 0) {
		LOG(SyncLayer, Error) << "Unable to set socket options: " << strerror(errno);
		goto err;
	}

	mreq.imr_multiaddr.s_addr = inet_addr(data->group.c_str());
	if (setsockopt(data->socket.get(), IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof(mreq)) < 0) {
		LOG(SyncLayer, Error) << "Unable to set socket options: " << strerror(errno);
		goto err;
	}

	if (bind(data->socket.get(), (struct sockaddr *)&addr, sizeof(addr)) < 0) {
		LOG(SyncLayer, Error) << "Unable to bind client socket: " << strerror(errno);
		goto err;
	}

	/*
	 * For the client, flush anything in the socket. It might be stale from a previous sync run,
	 * or we might get another packet in a frame to two before the adjustment caused by this (old)
	 * packet, although correct, had taken effect. So this keeps things simpler.
	 */
	addrlen = sizeof(addr);
	ret = 0;
	while (ret >= 0) {
		ret = recvfrom(data->socket.get(),
			       &payload, sizeof(payload), 0,
			       (struct sockaddr *)&addr, &addrlen);
	}

	return;

err:
	data->socket.reset();
}

void processControls(SyncLayerData *data, ControlList &controls)
{
	auto intf = controls.get<std::string_view>(controls::SyncInterface);
	if (intf)
		data->networkInterface = *intf;

	auto mode = controls.get<int32_t>(controls::rpi::SyncMode);
	if (mode) {
		data->mode = static_cast<Mode>(*mode);
		if (data->mode == Mode::Off) {
			reset(data);
		} else {
			/*
			 * This goes here instead of init() because we need the control
			 * to tell us whether we're server or client
			 */
			initializeSocket(data);
		}
	}

	auto syncFrames = controls.get<int32_t>(controls::rpi::SyncFrames);
	if (syncFrames && *syncFrames > 0)
		data->readyFrame = *syncFrames;

	auto frameDurationLimits = controls.get(controls::FrameDurationLimits);
	if (frameDurationLimits)
		data->frameDuration = std::chrono::microseconds((*frameDurationLimits)[0]);

	/*
	 * \todo Should we just let SyncAdjustment through as-is if the
	 * application provides it? Maybe it wants to do sync itself without
	 * the layer, but the layer has been loaded anyway
	 */
}

/*
 * Camera sync algorithm.
 *     Server - there is a single server that sends framerate timing information over the network to any
 *         clients that are listening. It also signals when it will send a "everything is synchronised, now go"
 *         message back to the algorithm.
 *     Client - there may be many clients, either on the same device or different ones. They match their
 *         framerates to the server, and indicate when to "go" at the same instant as the server.
 */
void processFrame(SyncLayerData *data, uint64_t frameWallClock,
		  ControlList &metadata)
{
	/* frameWallClock has already been de-jittered for us. Convert from ns into us. */
	uint64_t wallClockFrameTimestamp = frameWallClock / 1000;

	/*
	 * This is the headline frame duration in microseconds as programmed into the sensor. Strictly,
	 * the sensor might not quite match the system clock, but this shouldn't matter for the calculations
	 * we'll do with it, unless it's a very very long way out!
	 */
	uint32_t frameDuration = data->frameDuration.get<std::micro>();

	/* Timestamps tell us if we've dropped any frames, but we still want to count them. */
	unsigned int droppedFrames = 0;
	/* Count dropped frames into the frame counter */
	if (data->frameCount) {
		wallClockFrameTimestamp =
			std::max<uint64_t>(wallClockFrameTimestamp,
					   data->lastWallClockFrameTimestamp + frameDuration / 2);
		/*
		 * Round down here, because data->frameCount gets incremented
		 * at the end of the function.
		 */
		droppedFrames =
			(wallClockFrameTimestamp - data->lastWallClockFrameTimestamp - frameDuration / 2) / frameDuration;
		data->frameCount += droppedFrames;
	}

	if (data->mode == Mode::Server)
		processFrameServer(data, wallClockFrameTimestamp,
				   metadata, droppedFrames);
	else if (data->mode == Mode::Client)
		processFrameClient(data, wallClockFrameTimestamp,
				   metadata);

	data->lastWallClockFrameTimestamp = wallClockFrameTimestamp;

	metadata.set(controls::rpi::SyncReady, data->syncReady);

	data->frameCount++;
}

void processFrameServer(SyncLayerData *data, int64_t wallClockFrameTimestamp,
			ControlList &metadata, unsigned int droppedFrames)
{
	uint32_t frameDuration = data->frameDuration.get<std::micro>();

	/*
	 * Server sends a packet every syncPeriod frames, or as soon after as possible (if any
	 * frames were dropped).
	 */
	data->serverFrameCountPeriod += droppedFrames;

	if (data->frameCount == 0) {
		data->frameDurationEstimated = frameDuration;
	} else {
		double diff = (wallClockFrameTimestamp - data->lastWallClockFrameTimestamp) / (1 + droppedFrames);
		unsigned int N = std::min(data->frameCount, 99U);
		/*
		 * Smooth out the variance of the frame duration estimation to
		 * get closer to the true frame duration
		 */
		data->frameDurationEstimated = data->frameCount == 1 ? diff
					     : (N * data->frameDurationEstimated + diff) / (N + 1);
	}

	/* Calculate frames remaining, and therefore "time left until ready". */
	int framesRemaining = data->readyFrame - data->frameCount;
	uint64_t wallClockReadyTime = wallClockFrameTimestamp + (int64_t)framesRemaining * data->frameDurationEstimated;

	if (data->serverFrameCountPeriod >= data->syncPeriod) {
		/* It's time to sync */
		data->serverFrameCountPeriod = 0;

		SyncPayload payload;
		/* round to nearest us */
		payload.frameDuration = data->frameDurationEstimated + .5;
		payload.wallClockFrameTimestamp = wallClockFrameTimestamp;
		payload.wallClockReadyTime = wallClockReadyTime;

		LOG(SyncLayer, Debug) << "Send packet (frameNumber " << data->frameCount << "):";
		LOG(SyncLayer, Debug) << "            frameDuration " << payload.frameDuration;
		LOG(SyncLayer, Debug) << "            wallClockFrameTimestamp " << wallClockFrameTimestamp
				      << " (" << wallClockFrameTimestamp - data->lastWallClockFrameTimestamp << ")";
		LOG(SyncLayer, Debug) << "            wallClockReadyTime " << wallClockReadyTime;

		if (sendto(data->socket.get(), &payload, sizeof(payload), 0, (const sockaddr *)&data->addr, sizeof(data->addr)) < 0)
			LOG(SyncLayer, Error) << "Send error! " << strerror(errno);
	}

	int64_t timerValue = static_cast<int64_t>(wallClockReadyTime - wallClockFrameTimestamp);
	if (!data->syncReady && wallClockFrameTimestamp + data->frameDurationEstimated / 2 > wallClockReadyTime) {
		data->syncReady = true;
		LOG(SyncLayer, Info) << "*** Sync achieved! Difference " << timerValue << "us";
	}

	/* Server always reports this */
	metadata.set(controls::rpi::SyncTimer, timerValue);

	data->serverFrameCountPeriod += 1;
}

void processFrameClient(SyncLayerData *data, int64_t wallClockFrameTimestamp,
			ControlList &metadata)
{
	uint64_t serverFrameTimestamp = 0;
	SyncPayload payload;

	bool packetReceived = false;
	while (true) {
		ssize_t ret = recv(data->socket.get(), &payload, sizeof(payload), 0);

		if (ret != sizeof(payload))
			break;
		packetReceived = true;
		data->clientSeenPacket = true;

		data->frameDurationEstimated = payload.frameDuration;
		serverFrameTimestamp = payload.wallClockFrameTimestamp;
		data->serverReadyTime = payload.wallClockReadyTime;
	}

	if (packetReceived) {
		uint64_t clientFrameTimestamp = wallClockFrameTimestamp;
		int64_t clientServerDelta = clientFrameTimestamp - serverFrameTimestamp;
		/* "A few frames ago" may have better matched the server's frame. Calculate when it was. */
		int framePeriodErrors = (clientServerDelta + data->frameDurationEstimated / 2) / data->frameDurationEstimated;
		int64_t clientFrameTimestampNearest = clientFrameTimestamp - framePeriodErrors * data->frameDurationEstimated;
		/* We must shorten a single client frame by this amount if it exceeds the minimum: */
		int32_t correction = clientFrameTimestampNearest - serverFrameTimestamp;
		if (std::abs(correction) < data->minAdjustment)
			correction = 0;

		LOG(SyncLayer, Debug) << "Received packet (frameNumber " << data->frameCount << "):";
		LOG(SyncLayer, Debug) << "                serverFrameTimestamp " << serverFrameTimestamp;
		LOG(SyncLayer, Debug) << "                serverReadyTime " << data->serverReadyTime;
		LOG(SyncLayer, Debug) << "                clientFrameTimestamp " << clientFrameTimestamp;
		LOG(SyncLayer, Debug) << "                clientFrameTimestampNearest " << clientFrameTimestampNearest
				      << " (" << framePeriodErrors << ")";
		LOG(SyncLayer, Debug) << "                correction " << correction;

		data->frameDurationOffset = correction * 1us;
	}

	int64_t timerValue = static_cast<int64_t>(data->serverReadyTime - wallClockFrameTimestamp);
	if (data->clientSeenPacket && !data->syncReady && wallClockFrameTimestamp + data->frameDurationEstimated / 2 > data->serverReadyTime) {
		data->syncReady = true;
		LOG(SyncLayer, Info) << "*** Sync achieved! Difference " << timerValue << "us";
	}

	/* Client reports this once it receives it from the server */
	if (data->clientSeenPacket)
		metadata.set(controls::rpi::SyncTimer, timerValue);
}

namespace libcamera {

extern "C" {

[[gnu::visibility("default")]]
struct LayerInfo layerInfo{
	.name = "sync",
	.layerAPIVersion = 1,
};

[[gnu::visibility("default")]]
struct LayerInterface layerInterface{
	.init = init,
	.terminate = terminate,
	.bufferCompleted = nullptr,
	.requestCompleted = requestCompleted,
	.disconnected = nullptr,
	.acquire = nullptr,
	.release = nullptr,
	.controls = updateControls,
	.properties = nullptr,
	.configure = nullptr,
	.createRequest = nullptr,
	.queueRequest = queueRequest,
	.start = start,
	.stop = nullptr,
};
}

} /* namespace libcamera */
