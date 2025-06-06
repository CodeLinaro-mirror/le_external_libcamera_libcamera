/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * Copyright (C) 2025, Ideas On Board Oy
 */

#include <libcamera/metadata_list.h>

namespace libcamera {

/**
 * \class MetadataListPlan
 * \brief Class to hold the possible set of metadata items for a \ref MetadataList
 */

/**
 * \fn MetadataListPlan::begin() const
 * \brief Retrieve the begin iterator
 */

/**
 * \fn MetadataListPlan::end() const
 * \brief Retrieve the end iterator
 */

/**
 * \fn MetadataListPlan::size() const
 * \brief Retrieve the number of controls
 */

/**
 * \fn MetadataListPlan::empty() const
 * \brief Check if empty
 */

/**
 * \fn MetadataListPlan::clear()
 * \brief Remove all controls
 */

/**
 * \fn MetadataListPlan::add(const Control<T> &ctrl)
 * \brief Add a control to the metadata list plan
 */

/**
 * \fn MetadataListPlan::add(const Control<T> &ctrl, std::size_t count)
 * \brief Add a dynamically-sized control to the metadata list plan
 * \param[in] ctrl The control
 * \param[in] count The maximum number of elements
 *
 * Add the dynamically-sized control \a ctrl to the metadata list plan with a maximum
 * capacity of \a count elements.
 */

/**
 * \fn MetadataListPlan::remove(std::uint32_t tag)
 * \brief Remove the entry with given identifier from the plan
 */

/**
 * \fn MetadataListPlan::remove(const ControlId &ctrl)
 * \brief Remove \a ctrl from the plan
 */

/**
 * \class MetadataList
 * \brief Class to hold metadata items
 */

/**
 * \fn MetadataList::MetadataList(const MetadataListPlan &plan)
 * \brief Construct a metadata list according to \a plan
 *
 * Construct a metadata list according to the provided \a plan.
 */

/**
 * \fn MetadataList::MetadataList(const MetadataList &other)
 * \brief Copy constructor
 * \context This function is \threadsafe wrt. \a other.
 */

/**
 * \fn MetadataList::size() const
 * \brief Retrieve the number of controls
 * \context This function is \threadsafe.
 * \note If the list is being modified, the return value may be out of
 *       date by the time the function returns
 */

/**
 * \fn MetadataList::empty() const
 * \brief Check if empty
 * \context This function is \threadsafe.
 * \note If the list is being modified, the return value may be out of
 *       date by the time the function returns
 */

/**
 * \internal
 * \fn MetadataList::clear()
 * \brief Remove all items from the list
 * \note This function in effect resets the list to its original state. As a consequence it invalidates - among others -
 *       all iterators, Checkpoint, and Diff objects that are associated with the list. No readers must exist
 *       when this function is called.
 */

/**
 * \fn MetadataList::begin() const
 * \brief Retrieve begin iterator
 * \context This function is \threadsafe.
 */

/**
 * \fn MetadataList::end() const
 * \brief Retrieve end iterator
 * \context This function is \threadsafe.
 */

/**
 * \fn MetadataList::get(const Control<T> &ctrl) const
 * \brief Get the value of control \a ctrl
 * \return A std::optional<T> containing the control value, or std::nullopt if
 *         the control \a ctrl is not present in the list
 * \context This function is \threadsafe.
 */

/**
 * \fn MetadataList::get(std::uint32_t tag) const
 * \brief Get the value of pertaining to the numeric identifier \a tag
 * \return A std::optional<T> containing the control value, or std::nullopt if
 *         the control is not present in the list
 * \context This function is \threadsafe.
 */

/**
 * \fn MetadataList::set(const Control<T> &ctrl, const details::cxx20::type_identity_t<T> &value)
 * \brief Set the value of control \a ctrl to \a value
 */

/**
 * \fn MetadataList::set(std::uint32_t tag, ControlValueView v)
 * \brief Set the value of pertaining to the numeric identifier \a tag to \a v
 */

/**
 * \internal
 * \fn MetadataList::merge(const MetadataList &other)
 * \brief Add all missing items from \a other
 *
 * Add all items from \a other that are not present in \a this. If an item
 * has a numeric identifier that was not present in the MetadataListPlan
 * used to construct \a this, then the item is ignored.
 *
 * \context This function is \threadsafe wrt. \a other.
 */

/**
 * \internal
 * \fn MetadataList::merge(const ControlList &other)
 * \copydoc MetadataList::merge(const MetadataList &other)
 */

/**
 * \enum MetadataList::SetError
 * \brief TODO
 *
 * \var MetadataList::SetError::UnknownTag
 * \brief The tag is not supported by the metadata list
 * \var MetadataList::SetError::AlreadySet
 * \brief A value has already been added with the given tag
 * \var MetadataList::SetError::DataTooLarge
 * \brief The data is too large for the given tag
 * \var MetadataList::SetError::TypeMismatch
 * \brief The type of the value does not match the expected type
 */

/**
 * \internal
 * \fn MetadataList::checkpoint() const
 * \brief Create a checkpoint
 * \context This function is \threadsafe.
 */

/**
 * \class MetadataList::iterator
 * \brief Iterator
 */

/**
 * \typedef MetadataList::iterator::difference_type
 * \brief iterator's difference type
 */

/**
 * \typedef MetadataList::iterator::value_type
 * \brief iterator's value type
 */

/**
 * \typedef MetadataList::iterator::pointer
 * \brief iterator's pointer type
 */

/**
 * \typedef MetadataList::iterator::reference
 * \brief iterator's reference type
 */

/**
 * \typedef MetadataList::iterator::iterator_category
 * \brief iterator's category
 */

/**
 * \fn MetadataList::iterator::operator*()
 * \brief Retrieve value at iterator
 * \return A \a ControlListView representing the value
 */

/**
 * \fn MetadataList::iterator::operator==(const iterator &other) const
 * \brief Check if two iterators are equal
 */

/**
 * \fn MetadataList::iterator::operator!=(const iterator &other) const
 * \brief Check if two iterators are not equal
 */

/**
 * \fn MetadataList::iterator::operator++(int)
 * \brief Advance the iterator
 */

/**
 * \fn MetadataList::iterator::operator++()
 * \brief Advance the iterator
 */

/**
 * \class MetadataList::Diff
 * \brief Designates a set of consecutively added metadata items from a particular MetadataList
 * \sa Camera::metadataAvailable
 * \internal
 * \sa MetadataList::Checkpoint::diffSince()
 * \endinternal
 */

/**
 * \fn MetadataList::Diff::list() const
 * \brief Retrieve the associated MetadataList
 */

/**
 * \fn MetadataList::Diff::size() const
 * \brief Retrieve the number of metadata items designated
 */

/**
 * \fn MetadataList::Diff::empty() const
 * \brief Check if no metadata items are designated
 */

/**
 * \fn MetadataList::Diff::operator bool() const
 * \copydoc MetadataList::Diff::empty() const
 */

/**
 * \fn MetadataList::Diff::get(const Control<T> &ctrl) const
 * \copydoc MetadataList::get(const Control<T> &ctrl) const
 * \note The value pertaining to \a ctrl will only be returned if it is part of Diff,
 *       meaning that even if \a ctrl is part of the backing MetadataList, it will not
 *       be returned if \a ctrl is not in the set of controls designated by this Diff object.
 */

/**
 * \fn MetadataList::Diff::get(std::uint32_t tag) const
 * \copydoc MetadataList::Diff::get(const Control<T>&ctrl) const
 */

/**
 * \fn MetadataList::Diff::begin() const
 * \brief Retrieve the begin iterator
 */

/**
 * \fn MetadataList::Diff::end() const
 * \brief Retrieve the end iterator
 */

/**
 * \internal
 * \class MetadataList::Checkpoint
 * \brief Designates a point in the stream of metadata items
 *
 * A Checkpoint object designates a point in the stream of metadata items in the associated
 * MetadataList. Its main use to be able to retrieve the set of metadata items that were
 * added to the list after the designated point using diffSince().
 */

/**
 * \internal
 * \fn MetadataList::Checkpoint::list() const
 * \brief Retrieve the associated \ref MetadataList
 */

/**
 * \internal
 * \fn MetadataList::Checkpoint::diffSince() const
 * \brief Retrieve the set of metadata items added since the checkpoint was created
 */

} /* namespace libcamera */
