/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * Copyright (C) 2025, Ideas On Board Oy
 */

#include <libcamera/metadata_list.h>

namespace libcamera {

/**
 * \class MetadataListPlan
 * \brief Class to hold the possible set of metadata items for a MetadataList
 */

/**
 * \class MetadataListPlan::Entry
 * \brief Details of a metadata item
 */

/**
 * \internal
 * \var MetadataListPlan::Entry::size
 * \brief Number of bytes in a single element
 *
 * \var MetadataListPlan::Entry::alignment
 * \brief Required alignment of the elements
 * \endinternal
 *
 * \var MetadataListPlan::Entry::numElements
 * \brief Number of elements in the value
 * \sa ControlValueView::numElements()
 *
 * \var MetadataListPlan::Entry::type
 * \brief The type of the value
 * \sa ControlValueView::type()
 *
 * \var MetadataListPlan::Entry::isArray
 * \brief Whether or not the value is array-like
 * \sa ControlValueView::isArray()
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
 * \brief Retrieve the number of entries
 */

/**
 * \fn MetadataListPlan::empty() const
 * \brief Check if empty
 */

/**
 * \internal
 * \fn MetadataListPlan::clear()
 * \brief Remove all controls
 */

/**
 * \internal
 * \fn MetadataListPlan::set(const Control<T> &ctrl)
 * \brief Add an entry for the given control to the metadata list plan
 * \param[in] ctrl The control
 */

/**
 * \internal
 * \fn MetadataListPlan::set(const Control<T> &ctrl, std::size_t count)
 * \brief Add an entry for the given dynamically-sized control to the metadata list plan
 * \param[in] ctrl The control
 * \param[in] count The maximum number of elements
 *
 * Add the dynamically-sized control \a ctrl to the metadata list plan with a maximum
 * capacity of \a count elements.
 */

/**
 * \internal
 * \fn MetadataListPlan::set(std::uint32_t tag,
 *			     std::size_t size, std::size_t alignment,
 *			     std::size_t count, ControlType type, bool isArray)
 * \brief Add an entry to the metadata list plan
 * \return \a true if the entry has been added, or \a false if the given parameters
 *         would result in an invalid entry
 *
 * This functions adds an entry with essentially arbitrary parameters, without deriving
 * them from a given ControlId instance. This is mainly used when deserializing.
 */

/**
 * \fn MetadataListPlan::get(std::uint32_t tag) const
 * \brief Find the \ref Entry "entry" with the given identifier
 */

/**
 * \fn MetadataListPlan::get(const ControlId &cid) const
 * \brief Find the \ref Entry "entry" for the given ControlId
 *
 * The \ref Entry "entry" is only returned if ControlId::type() and ControlId::isArray()
 * of \a cid matches Entry::type and Entry::isArray, respectively.
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
 * \internal
 * \fn MetadataList::set(const Control<T> &ctrl, const details::cxx20::type_identity_t<T> &value)
 * \brief Set the value of control \a ctrl to \a value
 */

/**
 * \internal
 * \fn MetadataList::set(std::uint32_t tag, ControlValueView v)
 * \brief Set the value of pertaining to the numeric identifier \a tag to \a v
 */

/**
 * \internal
 * \fn MetadataList::merge(const ControlList &other)
 * \brief Add all missing items from \a other
 *
 * Add all items from \a other that are not present in \a this.
 */

/**
 * \internal
 * \enum MetadataList::SetError
 * \brief Error code returned by a set operation
 *
 * \var MetadataList::SetError::UnknownTag
 * \brief The tag is not supported by the metadata list
 * \var MetadataList::SetError::AlreadySet
 * \brief A value has already been added with the given tag
 * \var MetadataList::SetError::SizeMismatch
 * \brief The size of the data is not appropriate for the given tag
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
 * \brief Check if any metadata items are designated
 */

/**
 * \fn MetadataList::Diff::operator bool() const
 * \copydoc MetadataList::Diff::empty() const
 */

/**
 * \fn MetadataList::Diff::get(const Control<T> &ctrl) const
 * \copydoc MetadataList::get(const Control<T> &ctrl) const
 * \note The lookup will fail if the metadata item is not designated by this Diff object,
 *       even if it is otherwise present in the backing MetadataList.
 */

/**
 * \fn MetadataList::Diff::get(std::uint32_t tag) const
 * \copydoc MetadataList::get(std::uint32_t tag) const
 * \note The lookup will fail if the metadata item is not designated by this Diff object,
 *       even if it is otherwise present in the backing MetadataList.
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
 * \fn MetadataList::Checkpoint::diffSince() const
 * \brief Retrieve the set of metadata items added since the checkpoint was created
 */

} /* namespace libcamera */
