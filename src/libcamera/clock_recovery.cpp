/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * Copyright (C) 2024, Raspberry Pi Ltd
 *
 * Camera sync control algorithm
 */

#include "libcamera/internal/clock_recovery.h"

#include <libcamera/base/log.h>

using namespace libcamera;

LOG_DEFINE_CATEGORY(RPiClockRec)

ClockRecovery::ClockRecovery()
{
	initialise();
}

void ClockRecovery::initialise(unsigned int numPts, unsigned int maxJitter, unsigned int minPts,
			       unsigned int errorThreshold)
{
	numPts_ = numPts;
	maxJitter_ = maxJitter;
	minPts_ = minPts;
	errorThreshold_ = errorThreshold;
	reset();
}

void ClockRecovery::reset()
{
	lastInput_ = 0;
	lastOutput_ = 0;
	xAve_ = 0;
	yAve_ = 0;
	x2Ave_ = 0;
	xyAve_ = 0;
	count_ = 0;
	slope_ = 0.0;
	offset_ = 0.0;
	error_ = 0.0;
}

void ClockRecovery::addSample(uint64_t input, uint64_t output)
{
	if (count_ == 0) {
		inputBase_ = input;
		outputBase_ = output;
	}

	/*
	 * We keep an eye on cumulative drift over the last several frames. If this exceeds a
	 * threshold, then probably the system clock has been updated and we're going to have to
	 * reset everything and start over.
	 */
	if (lastOutput_) {
		int64_t inputDiff = getOutput(input) - getOutput(lastInput_);
		int64_t outputDiff = output - lastOutput_;
		error_ = error_ * 0.95 + (outputDiff - inputDiff);
		if (std::abs(error_) > errorThreshold_) {
			reset();
			inputBase_ = input;
			outputBase_ = output;
		}
	}
	lastInput_ = input;
	lastOutput_ = output;

	/*
	 * Never let the new output value be more than maxJitter_ away from what we would have expected.
	 * This is just to reduce the effect of sudden large delays in the measured output.
	 */
	uint64_t expectedOutput = getOutput(input);
	output = std::clamp(output, expectedOutput - maxJitter_, expectedOutput + maxJitter_);

	/*
	 * We use x, y, x^2 and x*y sums to calculate the best fit line. Here we update them by
	 * pretending we have count_ samples at the previous fit, and now one new one. Gradually
	 * the effect of the older values gets lost. This is a very simple way of updating the
	 * fit (there are much more complicated ones!), but it works well enough. Using averages
	 * instead of sums makes the relative effect of old values and the new sample clearer.
	 */
	double x = input - inputBase_;
	double y = output - outputBase_ - x;
	unsigned int count1 = count_ + 1;
	xAve_ = (count_ * xAve_ + x) / count1;
	yAve_ = (count_ * yAve_ + y) / count1;
	x2Ave_ = (count_ * x2Ave_ + x * x) / count1;
	xyAve_ = (count_ * xyAve_ + x * y) / count1;

	/* Don't update slope and offset until we've seen "enough" sample points. */
	if (count_ > minPts_) {
		/* These are the standard equations for least squares linear regression. */
		slope_ = (count1 * count1 * xyAve_ - count1 * xAve_ * count1 * yAve_) /
			 (count1 * count1 * x2Ave_ - count1 * xAve_ * count1 * xAve_);
		offset_ = yAve_ - slope_ * xAve_;
	}

	/* Don't increase count_ above numPts_, as this controls the long-term amount of the residual fit. */
	if (count1 < numPts_)
		count_++;
}

uint64_t ClockRecovery::getOutput(uint64_t input)
{
	double x = input - inputBase_;
	double y = slope_ * x + offset_;
	return y + x + outputBase_;
}
