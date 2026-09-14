/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * Copyright (C) 2026, Ideas on Board.
 *
 * Helper to synchronize buffer sequences
 */

#include "libcamera/internal/sequence_sync_helper.h"

#include <libcamera/base/log.h>

namespace libcamera {

LOG_DEFINE_CATEGORY(SequenceSyncHelper)

/**
 * \file sequence_sync_helper.h
 * \class SequenceSyncHelper
 * \brief Helper to synchronize buffer sequences
 *
 * On a V4L2 buffers the sequence is not known until the buffer was dequeued. To
 * pre plan regulation it is however necessary to know the sequence of a buffer
 * before queueing the buffer (or multiple buffers). By the time an offset is
 * detected in most cases more than one buffers are already queued in and all of
 * them carry the same error. Simple adding the perceived difference on dequeue
 * therefore doesn't help and leads to oscillations. This class tracks the
 * sequence corrections over time and helps in keeping the sequence numbers in
 * sync.
 */

/**
 * \brief Tell the sync helper that a frame was received
 * \param expectedSequence The sequence that was expected for that frame
 * \param actualSequence The actual sequence of the frame
 *
 * This function needs to be called when a frame was dequeued. The sync helper
 * calculates necessary corrections and keeps track of corrections already
 * applied.
 */
int SequenceSyncHelper::receivedFrame(size_t expectedSequence,
				      size_t actualSequence)
{
	ASSERT(!corrections_.empty());
	int diff = actualSequence - expectedSequence;
	int corr = corrections_.front();
	corrections_.pop();
	expectedOffset_ -= corr;
	int necessaryCorrection = diff - expectedOffset_;
	correctionToApply_ += necessaryCorrection;

	LOG(SequenceSyncHelper, Debug)
		<< "Sync frame "
		<< "expected: " << expectedSequence
		<< " actual: " << actualSequence
		<< " correction: " << corr
		<< " expectedOffset: " << expectedOffset_
		<< " correctionToApply " << correctionToApply_;

	expectedOffset_ += necessaryCorrection;
	return necessaryCorrection;
}

/**
 * \brief Tell the sync helper that a frame was cancelled
 *
 * This function needs to be called when a frame was cancelled.
 */
void SequenceSyncHelper::cancelFrame()
{
	int corr = corrections_.front();
	corrections_.pop();
	expectedOffset_ -= corr;
}

/**
 * \brief Get the necessary correction
 *
 * Get the correction that must be applied to the sequence numbers to
 * synchronize.
 *
 * \return The correction to apply
 */
int SequenceSyncHelper::correction()
{
	return correctionToApply_;
}

/**
 * \brief Tell the sync helper that a correction was pushed
 *
 * This must be called for every frame that gets pushed into the queue. If
 * no correction was applied, it must be called with a correction of 0.
 */
void SequenceSyncHelper::pushCorrection(int correction)
{
	corrections_.push(correction);
	correctionToApply_ -= correction;
	LOG(SequenceSyncHelper, Debug)
		<< "Push correction " << correction
		<< " correctionToApply " << correctionToApply_;
}

/**
 * \brief Reset the sync helper
 */
void SequenceSyncHelper::reset()
{
	corrections_ = {};
	correctionToApply_ = 0;
	expectedOffset_ = 0;
}

} /* namespace libcamera */
