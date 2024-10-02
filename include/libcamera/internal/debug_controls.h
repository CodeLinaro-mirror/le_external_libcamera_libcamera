/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * Copyright (C) 2024, Ideas on Board Oy
 *
 * Debug metadata helpers
 */

#pragma once

#include <libcamera/control_ids.h>

namespace libcamera {

class DebugMetadata
{
public:
	DebugMetadata() = default;

	void checkForEnable(const ControlList &controls);
	void enable(bool enable = true);
	void assignUpstream(DebugMetadata *upstream);
	void assignControlList(ControlList *list);

	template<typename T, typename V>
	void set(const Control<T> &ctrl, const V &value)
	{
		if (upstream_) {
			upstream_->set<T, V>(ctrl, value);
			return;
		}

		if (!enabled_)
			return;

		if (list_) {
			list_->set<T, V>(ctrl, value);
			return;
		}

		cache_.set<T, V>(ctrl, value);
	}

	void set(unsigned int id, const ControlValue &value);

private:
	bool enabled_ = false;
	ControlList *list_ = nullptr;
	DebugMetadata *upstream_ = nullptr;
	ControlList cache_;
};

} /* namespace libcamera */
