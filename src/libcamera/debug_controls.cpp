/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * Copyright (C) 2024, Ideas on Board Oy.
 *
 * Helper to easily record debug metadata inside libcamera.
 */

#include "libcamera/internal/debug_controls.h"

namespace libcamera {

/**
 * \file debug_controls.h
 * \brief Helper to easily record debug metadata inside libcamera
 */

/**
 * \class DebugMetadata
 * \brief Helper to record metadata for later use
 *
 * When one wants to record debug metadata, the metadata list is often not
 * directly available (either because we are inside process() of an IPA or
 * because we are in a closed module). This class allows to record the data and
 * at a later point in time forward it either to another DebugMetadata instance
 * or to a ControlList.
 */

/**
 * \fn DebugMetadata::checkForEnable
 * \brief Check for DebugMetadataEnable in the supplied ControlList
 * \param[in] controls The supplied ControlList
 *
 * Looks for controls::DebugMetadataEnable and enables or disables debug
 * metadata handling accordingly.
 */
void DebugMetadata::checkForEnable(const ControlList &controls)
{
	const auto &ctrl = controls.get(controls::DebugMetadataEnable);
	if (ctrl)
		enable(*ctrl);
}

/**
 * \fn DebugMetadata::enable
 * \brief Enables or disabled metadata handling
 * \param[in] enable The enable state
 *
 * Enables or disables metadata handling according to \a enable. When enable is
 * false, the cache gets cleared and no further metadata is recorded.
 */
void DebugMetadata::enable(bool enable)
{
	enabled_ = enable;
	if (!enabled_)
		cache_.clear();
}

/**
 * \fn DebugMetadata::assignUpstream
 * \brief Assign an upstream metadata handler
 * \param[in] upstream Pointer to the upstream handler
 *
 * When a upstream gets set, the cache is copies to that list and all further
 * calls to DebugMetadata::set are forwarded to that list.
 *
 * The upstream can be reset by passing a nullptr.
 */
void DebugMetadata::assignUpstream(DebugMetadata *upstream)
{
	upstream_ = upstream;

	if (!upstream_)
		return;

	for (const auto &ctrl : cache_)
		upstream_->set(ctrl.first, ctrl.second);

	cache_.clear();
}

/**
 * \fn DebugMetadata::assignControlList
 * \brief Assign a control list
 * \param[in] list Pointer to the list
 *
 * When a list gets set, the cache is copies to that list and all further calls
 * to DebugMetadata::set are forwarded to that list.
 *
 * The upstream can be reset by passing a nullptr.
 */
void DebugMetadata::assignControlList(ControlList *list)
{
	list_ = list;

	if (list_) {
		list_->merge(cache_);
		cache_.clear();
	}
}

/**
 * \fn DebugMetadata::set(const Control<T> &ctrl, const V &value)
 * \brief Set a value
 * \param[in] ctrl The ctrl to set
 * \param[in] value The control value
 *
 * Sets the debug metadata for ctrl to value \a value. If an upstream or a
 * control list is set, the value gets passed there (upstream takes precedence).
 * Otherwise the value is cached until one of them gets assigned.
 *
 * When the instance is disabled and upstream is not set, the value gets dropped.
 */

/**
 * \fn DebugMetadata::set(unsigned int id, const ControlValue &value)
 * \brief Set a value
 * \param[in] id The id of the control
 * \param[in] value The control value
 *
 * Sets the debug metadata to value \a value. If an upstream or a control list
 * is set, the value gets passed there (upstream takes precedence).
 * Otherwise the value is cached until one of them gets assigned.
 *
 * When the instance is disabled and upstream is not set, the value gets dropped.
 */
void DebugMetadata::set(unsigned int id, const ControlValue &value)
{
	if (upstream_) {
		upstream_->set(id, value);
		return;
	}

	if (!enabled_)
		return;

	if (list_) {
		list_->set(id, value);
		return;
	}

	cache_.set(id, value);
}

} /* namespace libcamera */
