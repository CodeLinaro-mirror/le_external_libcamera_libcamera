/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * Copyright (C) 2024, Google Inc.
 *
 * Helper to easily record debug metadata inside libcamera.
 */

#include "libcamera/internal/debug_controls.h"

#include <libcamera/base/log.h>

namespace libcamera {

LOG_DEFINE_CATEGORY(DebugControls)

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
 * Enables or disables metadata handling according to \a enable. When \a enable
 * is true, all calls to set() get cached and can later be retrieved using \a
 * DebugMetadata::moveEntries(). When \a enable is false, the cache gets cleared
 * and no further metadata is recorded.
 *
 * Forwarding to a parent is independent of the enabled state.
 */
void DebugMetadata::enable(bool enable)
{
	enabled_ = enable;
	if (!enabled_)
		cache_.clear();
}

/**
 * \fn DebugMetadata::setParent
 * \brief Assign a parent metadata handler
 * \param[in] parent Pointer to the parent handler
 *
 * When a \a parent gets set, all further calls to DebugMetadata::set are
 * forwarded to that instance. It is not allowed to enable a DebugMetadata
 * object, log entries to it and later set the parent. This is done to keep a
 * path open for switching to tracing infrastructure later. For tracing one
 * would need some kind of "context" identifier that needs to be available on
 * set() time. The parent can be treated as such. The top level object
 * (the one where enable() get's called) lives in a place where that information
 * is also available.
 *
 * The parent can be reset by passing a nullptr.
 */
void DebugMetadata::setParent(DebugMetadata *parent)
{
	parent_ = parent;

	if (!parent_)
		return;

	if (!cache_.empty())
		LOG(DebugControls, Error)
			<< "Controls were recorded before setting a parent."
			<< " These are dropped.";

	cache_.clear();
}

/**
 * \fn DebugMetadata::moveEntries
 * \brief Move all cached entries into a list
 * \param[in] list The list
 *
 * Moves all entries into the list specified by \a list. Duplicate entries in
 * \a list get overwritten.
 */
void DebugMetadata::moveEntries(ControlList &list)
{
	list.merge(std::move(cache_), ControlList::MergePolicy::OverwriteExisting);
	cache_.clear();
}

/**
 * \fn DebugMetadata::set(const Control<T> &ctrl, const V &value)
 * \brief Set a value
 * \param[in] ctrl The ctrl to set
 * \param[in] value The control value
 *
 * Sets the debug metadata for \a ctrl to value \a value. If a parent is set,
 * the value gets passed there unconditionally. Otherwise it gets cached if the
 * instance is enabled or dropped silently when disabled.
 */

/**
 * \fn DebugMetadata::set(unsigned int id, const ControlValue &value)
 * \brief Set a value
 * \param[in] id The id of the control
 * \param[in] value The control value
 *
 * Sets the debug metadata for \a id to value \a value. If a parent is set,
 * the value gets passed there unconditionally. Otherwise it gets cached if the
 * instance is enabled or dropped silently when disabled.
 */
void DebugMetadata::set(unsigned int id, const ControlValue &value)
{
	if (parent_) {
		parent_->set(id, value);
		return;
	}

	if (!enabled_)
		return;

	cache_.set(id, value);
}

} /* namespace libcamera */
