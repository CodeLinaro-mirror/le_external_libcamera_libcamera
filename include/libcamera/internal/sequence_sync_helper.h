/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * Copyright (C) 2025, Ideas on Board
 *
 * Sequence sync helper
 */

#pragma once

#include <queue>

#include <libcamera/base/log.h>

namespace libcamera {

class SequenceSyncHelper
{
public:
	int receivedFrame(size_t expectedSequence, size_t actualSequence);
	void cancelFrame();
	int correction();
	void pushCorrection(int correction);
	void reset();

private:
	std::queue<int> corrections_;
	int correctionToApply_ = 0;
	int expectedOffset_ = 0;
};

} /* namespace libcamera */
