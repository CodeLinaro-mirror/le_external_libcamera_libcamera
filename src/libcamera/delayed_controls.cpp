/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * Copyright (C) 2020, Raspberry Pi Ltd
 *
 * delayed_controls.h - Helper to deal with controls that take effect with a delay
 */

#include "libcamera/internal/delayed_controls.h"

#include <libcamera/base/log.h>

#include <libcamera/controls.h>

#include "libcamera/internal/v4l2_device.h"

/**
 * \file delayed_controls.h
 * \brief Helper to deal with controls that take effect with a delay
 */

namespace libcamera {

LOG_DEFINE_CATEGORY(DelayedControls)

/**
 * \class DelayedControls
 * \brief Helper to deal with controls that take effect with a delay
 *
 * Some sensor controls take effect with a delay as the sensor needs time to
 * adjust, for example exposure and analog gain. This is a helper class to deal
 * with such controls and the intended users are pipeline handlers.
 *
 * The idea is to extend the concept of the buffer depth of a pipeline the
 * application needs to maintain to also cover controls. Just as with buffer
 * depth if the application keeps the number of requests queued above the
 * control depth the controls are guaranteed to take effect for the correct
 * request. The control depth is determined by the control with the greatest
 * delay.
 */

/**
 * \struct DelayedControls::ControlParams
 * \brief Parameters associated with controls handled by the \a DelayedControls
 * helper class
 *
 * \var ControlParams::delay
 * \brief Frame delay from setting the control on a sensor device to when it is
 * consumed during framing.
 *
 * \var ControlParams::priorityWrite
 * \brief Flag to indicate that this control must be applied ahead of, and
 * separately from the other controls.
 *
 * Typically set for the \a V4L2_CID_VBLANK control so that the device driver
 * does not reject \a V4L2_CID_EXPOSURE control values that may be outside of
 * the existing vertical blanking specified bounds, but are within the new
 * blanking bounds.
 */

/**
 * \brief Construct a DelayedControls instance
 * \param[in] device The V4L2 device the controls have to be applied to
 * \param[in] controlParams Map of the numerical V4L2 control ids to their
 * associated control parameters.
 *
 * The control parameters comprise of delays (in frames) and a priority write
 * flag. If this flag is set, the relevant control is written separately from,
 * and ahead of the rest of the batched controls.
 *
 * Only controls specified in \a controlParams are handled. If it's desired to
 * mix delayed controls and controls that take effect immediately the immediate
 * controls must be listed in the \a controlParams map with a delay value of 0.
 */
DelayedControls::DelayedControls(V4L2Device *device,
				 const std::unordered_map<uint32_t, ControlParams> &controlParams)
	: device_(device), maxDelay_(0)
{
	const ControlInfoMap &controls = device_->controls();

	/*
	 * Create a map of control ids to delays for controls exposed by the
	 * device.
	 */
	for (auto const &param : controlParams) {
		auto it = controls.find(param.first);
		if (it == controls.end()) {
			LOG(DelayedControls, Error)
				<< "Delay request for control id "
				<< utils::hex(param.first)
				<< " but control is not exposed by device "
				<< device_->deviceNode();
			continue;
		}

		const ControlId *id = it->first;

		controlParams_[id] = param.second;

		LOG(DelayedControls, Debug)
			<< "Set a delay of " << controlParams_[id].delay
			<< " and priority write flag " << controlParams_[id].priorityWrite
			<< " for " << id->name();

		maxDelay_ = std::max(maxDelay_, controlParams_[id].delay);
	}

	LOG(DelayedControls, Debug) << "Maximum delay: " << maxDelay_;

	reset();
}

/**
 * \brief Reset state machine
 *
 * Resets the state machine to a starting position based on control values
 * retrieved from the device.
 */
void DelayedControls::reset()
{
	queueIndex_ = 0;
	/* Frames up to maxDelay_ will be based on sensor init values. */
	writeIndex_ = maxDelay_;

	/* Retrieve control as reported by the device. */
	std::vector<uint32_t> ids;
	for (auto const &param : controlParams_)
		ids.push_back(param.first->id());

	ControlList controls = device_->getControls(ids);

	/* Seed the control queue with the controls reported by the device. */
	values_.clear();
	for (const auto &ctrl : controls) {
		const ControlId *id = device_->controls().idmap().at(ctrl.first);
		/*
		 * Do not mark this control value as updated, it does not need
		 * to be written to to device on startup.
		 */
		values_[id][0] = Info(ctrl.second, 0);
	}

	/* Copy state from previous frames. */
	for (auto &ctrl : values_) {
		for (auto i = queueIndex_; i < writeIndex_; i++) {
			Info &info = ctrl.second[i + 1];
			info = ctrl.second[i];
			info.sourceSequence_.reset();
		}
	}
}

/**
 * \brief Helper function to check if controls are queued already
 * \param[in] sequence Sequence number to check
 * \param[in] controls List of controls to compare against
 *
 * This function checks if the controls queued for frame \a sequence
 * are equal to \a controls. This is helpful in cases where a control algorithm
 * unconditionally queues controls for every frame, but always too late.
 * In that case this can be used to check if incoming controls are already queued
 * or need to be queued for a later frame.
 *
 * \returns true if \a controls are queued for the given sequence
 */
bool DelayedControls::controlsAreQueuedForFrame(unsigned int sequence, const ControlList &controls)
{
	auto idMap = controls.idMap();
	if (!idMap) {
		LOG(DelayedControls, Warning) << " idmap is null";
		return false;
	} else {
		for (const auto &[id, value] : controls) {
			if (values_[idMap->at(id)][sequence] != value) {
				return false;
			}
		}
	}
	return true;
}

/**
 * \brief Push a set of controls on the queue
 * \param[in] controls List of controls to add to the device queue
 * \deprecated This function is deprecated in favour of pushForFrame
 *
 * Push a set of controls to the control queue. This increases the control queue
 * depth by one.
 *
 * \returns See return value of DelayedControls::pushForFrame
 */
bool DelayedControls::push(const ControlList &controls)
{
	LOG(DelayedControls, Debug) << "Using deprecated function push(controls): " << queueIndex_;
	return pushForFrame(queueIndex_, controls);
}

/**
 * \brief Push a set of controls for a given frame
 * \param[in] sequence Sequence to push the controls for
 * \param[in] controls List of controls to add to the device queue
 *
 * Pushes the controls given by \a controls, to be applied at frame \a sequence.
 *
 * If there are controls already queued for that frame, these get updated.
 *
 * If it's too late for frame \a sequence (controls are already sent to the sensor),
 * the system checks if the controls that where written out for frame \a sequence
 * are the same as the requested ones. In this case, nothing is done.
 * If they differ, the controls get queued for the earliest frame possible
 * if no other controls with a higher sequence number are queued for that frame already.
 *
 * \returns true if \a controls are accepted, or false otherwise
 */

bool DelayedControls::pushForFrame(uint32_t sequence, const ControlList &controls)
{
	LOG(DelayedControls, Debug) << "push: " << sequence;
	auto idMap = controls.idMap();
	if (idMap) {
		for (const auto &[id, value] : controls) {
			LOG(DelayedControls, Debug) << "  " << idMap->at(id)->name() << " : " << value.toString();
		}
	}

	if (sequence < queueIndex_) {
		LOG(DelayedControls, Debug) << "Got updated data for frame:" << sequence;
	}

	if (sequence > queueIndex_) {
		LOG(DelayedControls, Warning) << "Hole in queue sequence. This should not happen. Expected: "
					      << queueIndex_ << " got " << sequence;
	}

	uint32_t updateIndex = sequence;
	/* check if its too late for the request */
	if (sequence < writeIndex_) {
		/* Check if we can safely ignore the request */
		if (controls.empty() || controlsAreQueuedForFrame(sequence, controls)) {
			if (sequence >= queueIndex_) {
				queueIndex_ = sequence + 1;
				return true;
			}
		} else {
			LOG(DelayedControls, Debug) << "Controls for frame " << sequence
						    << " are already in flight. Will be queued for frame " << writeIndex_;
			updateIndex = writeIndex_;
		}
	}

	if (static_cast<signed>(updateIndex) - static_cast<signed>(writeIndex_) >= listSize - static_cast<signed>(maxDelay_)) {
		/* The system is in an undefined state now. This will heal itself, as soon as all controls where rewritten */
		LOG(DelayedControls, Error) << "Queue length exceeded. The system is out of sync. Index to update:"
					    << updateIndex << " Next Index to apply: " << writeIndex_;
	}

	/**
	 * Prepare the ringbuffer entries with previous data.
	 * Data up to [writeIndex_] gets prepared in applyControls.
	 */
	if (updateIndex > writeIndex_ && updateIndex >= queueIndex_) {
		LOG(DelayedControls, Debug) << "Copy from previous " << queueIndex_;
		for (auto i = queueIndex_; i < updateIndex; i++) {
			/* Copy state from previous queue. */
			for (auto &ctrl : values_) {
				auto &ringBuffer = ctrl.second;
				ringBuffer[i] = ringBuffer[i - 1];
				ringBuffer[i].sourceSequence_.reset();
			}
		}
	}

	/* Update with new controls. */
	const ControlIdMap &idmap = device_->controls().idmap();
	for (const auto &control : controls) {
		const auto &it = idmap.find(control.first);
		if (it == idmap.end()) {
			LOG(DelayedControls, Warning)
				<< "Unknown control " << control.first;
			return false;
		}

		const ControlId *id = it->second;

		if (controlParams_.find(id) == controlParams_.end()) {
			LOG(DelayedControls, Error) << "Could not find params for control " << id << " ignored";
			continue;
		}

		Info &info = values_[id][updateIndex];
		/*
		 * Update the control only, if the already existing value stems from a request
		 * with a sequence number smaller or equal to the current one
		 */
		if (info.sourceSequence_.value_or(0) <= sequence) {
			info = Info(control.second, sequence);

			LOG(DelayedControls, Debug)
				<< "Queuing " << id->name()
				<< " to " << info.toString()
				<< " at index " << updateIndex;
		} else {
			LOG(DelayedControls, Warning)
				<< "Skipped update " << id->name()
				<< " at index " << updateIndex;
		}
	}

	LOG(DelayedControls, Debug) << "Queued frame: " << queueIndex_ << " it will be active in " << updateIndex;

	if (sequence >= queueIndex_)
		queueIndex_ = sequence + 1;

	return true;
}

/**
 * \brief Read back controls in effect at a sequence number
 * \param[in] sequence The sequence number to get controls for
 *
 * Read back what controls where in effect at a specific sequence number. The
 * history is a ring buffer of 16 entries where new and old values coexist. It's
 * the callers responsibility to not read too old sequence numbers that have been
 * pushed out of the history.
 *
 * Historic values are evicted by pushing new values onto the queue using
 * push(). The max history from the current sequence number that yields valid
 * values are thus 16 minus number of controls pushed.
 *
 * \return The controls at \a sequence number
 */
ControlList DelayedControls::get(uint32_t sequence)
{
	LOG(DelayedControls, Debug) << "get " << sequence << ":";
	ControlList out(device_->controls());
	for (const auto &ctrl : values_) {
		const ControlId *id = ctrl.first;
		const Info &info = ctrl.second[sequence];

		out.set(id->id(), info);

		LOG(DelayedControls, Debug)
			<< "  " << id->name()
			<< ": " << info.toString();
	}

	return out;
}

/**
 * \brief Inform DelayedControls of the start of a new frame
 * \param[in] sequence Sequence number of the frame that started (0-based)
 *
 * Inform the state machine that a new frame has started to arrive at the receiver
 * (e.g. the sensor started to clock out pixels) and of its sequence
 * number. This is usually the earliest point in time to update registers in the
 * sensor for upcoming frames. Any user of these helpers is responsible to inform
 * the helper about the start of any frame. This can be connected with ease to
 * the start of a exposure (SOE) V4L2 event.
 */
void DelayedControls::applyControls(uint32_t sequence)
{
	LOG(DelayedControls, Debug) << "Apply controls " << sequence;
	if (sequence != writeIndex_ - maxDelay_) {
		LOG(DelayedControls, Warning) << "Sequence and writeIndex are out of sync. Expected seq: " << writeIndex_ - maxDelay_ << " got " << sequence;
	}

	/*
	 * Create control list peeking ahead in the value queue to ensure
	 * values are set in time to satisfy the sensor delay.
	 */
	ControlList out(device_->controls());
	for (auto &ctrl : values_) {
		const ControlId *id = ctrl.first;
		unsigned int delayDiff = maxDelay_ - controlParams_[id].delay;
		unsigned int index = writeIndex_ - delayDiff;
		Info &info = ctrl.second[index];

		if (info.sourceSequence_.has_value()) {
			if (controlParams_[id].priorityWrite) {
				/*
				 * This control must be written now, it could
				 * affect validity of the other controls.
				 */
				ControlList priority(device_->controls());
				priority.set(id->id(), info);
				device_->setControls(&priority);
			} else {
				/*
				 * Batch up the list of controls and write them
				 * at the end of the function.
				 */
				out.set(id->id(), info);
			}

			LOG(DelayedControls, Debug)
				<< "Writing " << id->name()
				<< " (" << info.toString() << ") "
				<< " for frame " << index;
		}
	}

	auto oldWriteIndex = writeIndex_;
	writeIndex_ = sequence + maxDelay_ + 1;

	if (writeIndex_ >= queueIndex_ && writeIndex_ > oldWriteIndex) {
		LOG(DelayedControls, Debug)
			<< "Index " << writeIndex_ << " is not yet queued. Prepare with old state";
		/* Copy state from previous frames without resetting the sourceSequence */
		for (auto &ctrl : values_) {
			for (auto i = oldWriteIndex; i < writeIndex_; i++) {
				Info &info = ctrl.second[i + 1];
				info = values_[ctrl.first][i];
			}
		}
	}

	device_->setControls(&out);
}

} /* namespace libcamera */
