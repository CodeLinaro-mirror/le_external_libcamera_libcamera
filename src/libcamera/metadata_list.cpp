/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * Copyright (C) 2025, Ideas On Board Oy
 */

#include <libcamera/metadata_list.h>

#include <cstring>
#include <limits>
#include <new>

#include <libcamera/base/internal/align.h>
#include <libcamera/base/internal/cxx20.h>

#include <libcamera/metadata_list_plan.h>

#if __has_include(<sanitizer/asan_interface.h>)
#if __SANITIZE_ADDRESS__ /* gcc */
#include <sanitizer/asan_interface.h>
#define HAS_ASAN 1
#elif defined(__has_feature)
#if __has_feature(address_sanitizer) /* clang */
#include <sanitizer/asan_interface.h>
#define HAS_ASAN 1
#endif
#endif
#endif

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
 * \brief Add an entry to the metadata list plan
 * \return \a true if the entry has been added, or \a false if the given parameters
 *         would result in an invalid entry
 *
 * This functions adds an entry with essentially arbitrary parameters, without deriving
 * them from a given ControlId instance. This is mainly used when deserializing.
 */
bool MetadataListPlan::set(uint32_t tag,
			   std::size_t size, std::size_t alignment,
			   std::size_t numElements, ControlType type, bool isArray)
{
	if (size == 0 || size > std::numeric_limits<uint32_t>::max())
		return false;
	if (alignment > std::numeric_limits<uint32_t>::max())
		return false;
	if (!internal::cxx20::has_single_bit(alignment))
		return false;
	if (numElements > std::numeric_limits<uint32_t>::max() / size)
		return false;
	if (!isArray && numElements != 1)
		return false;

	items_[tag] = {
		.size = uint32_t(size),
		.alignment = uint32_t(alignment),
		.numElements = uint32_t(numElements),
		.type = type,
		.isArray = isArray,
	};

	return true;
}

/**
 * \fn MetadataListPlan::get(uint32_t tag) const
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
 *
 * Similarly to a ControlList, a MetadataList provides a way for applications to
 * query and enumerate the values of controls. However, a MetadataList allows
 * thread-safe access to the data for applications, which is needed so that
 * applications can process the metadata of in-flight \ref Request "requests"
 * (for which purposes ControlList is not suitable).
 *
 * \internal
 * A MetadataList is essentially an append-only list of values. Internally, it
 * contains a single allocation that is divided into two parts:
 *
 *   * a list of entries sorted by their numeric identifiers
 *     (each corresponding to an entry in the MetadataListPlan);
 *   * a series of ValueHeader + data bytes that contain the actual data.
 *
 * When a value is added to the list, the corresponding Entry is updated, and the
 * ValueHeader and the data bytes are appended to the end of the second part.
 *
 * The reason for the redundancy is the following: the first part enables quick
 * lookups (binary search); the second part provides a self-contained flat buffer
 * of all the data.
 */

/**
 * \internal
 * \brief Construct a metadata list according to \a plan
 *
 * Construct a metadata list according to the provided \a plan.
 */
MetadataList::MetadataList(const MetadataListPlan &plan)
	: capacity_(plan.size()),
	  contentOffset_(MetadataList::contentOffset(capacity_)),
	  alloc_(contentOffset_)
{
	for (const auto &[tag, e] : plan) {
		alloc_ += sizeof(ValueHeader);
		alloc_ += e.alignment - 1; // XXX: this is the maximum
		alloc_ += e.size * e.numElements;
		alloc_ += alignof(ValueHeader) - 1; // XXX: this is the maximum
	}

	p_ = static_cast<std::byte *>(::operator new(alloc_));

	auto *entries = reinterpret_cast<Entry *>(p_ + entriesOffset());
	auto it = plan.begin();

	for (std::size_t i = 0; i < capacity_; i++, ++it) {
		const auto &[tag, e] = *it;

		new (static_cast<void *>(&entries[i])) Entry{
			.tag = tag,
			.capacity = e.size * e.numElements,
			.alignment = e.alignment,
			.type = e.type,
			.isArray = e.isArray,
		};
	}

#if HAS_ASAN
	::__sanitizer_annotate_contiguous_container(
		p_ + contentOffset_, p_ + alloc_,
		p_ + alloc_, p_ + contentOffset_
	);
#endif
}

MetadataList::~MetadataList()
{
	for (auto &e : entries())
		std::destroy_at(&e);

#if HAS_ASAN
	/*
	 * The documentation says the range apparently has to be
	 * restored to its initial state before it is deallocated.
	 */
	::__sanitizer_annotate_contiguous_container(
		p_ + contentOffset_, p_ + alloc_,
		p_ + contentOffset_ + state_.load(std::memory_order_relaxed).fill, p_ + alloc_
	);
#endif

	::operator delete(p_, alloc_);
}

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
 * \brief Remove all items from the list
 * \note This function in effect resets the list to its original state. As a consequence it invalidates - among others -
 *       all iterators, Checkpoint, and Diff objects that are associated with the list. No readers must exist
 *       when this function is called.
 */
void MetadataList::clear()
{
	for (auto &e : entries())
		e.headerOffset.store(Entry::kInvalidOffset, std::memory_order_relaxed);

	[[maybe_unused]] State s = state_.exchange({}, std::memory_order_relaxed);

#if HAS_ASAN
	::__sanitizer_annotate_contiguous_container(
		p_ + contentOffset_, p_ + alloc_,
		p_ + contentOffset_ + s.fill, p_ + contentOffset_
	);
#endif
}


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
 * \fn MetadataList::get(uint32_t tag) const
 * \brief Get the value of pertaining to the numeric identifier \a tag
 * \return A std::optional<T> containing the control value, or std::nullopt if
 *         the control is not present in the list
 * \context This function is \threadsafe.
 */

/**
 * \internal
 * \fn MetadataList::set(const Control<T> &ctrl, const internal::cxx20::type_identity_t<T> &value)
 * \brief Set the value of control \a ctrl to \a value
 */

/**
 * \internal
 * \fn MetadataList::set(uint32_t tag, ControlValueView v)
 * \brief Set the value of pertaining to the numeric identifier \a tag to \a v
 */

/**
 * \internal
 * \brief Add items from \a other
 *
 * If any of them items cannot be added, then an empty optional is returned,
 * and this function has no effects.
 */
std::optional<MetadataList::Diff> MetadataList::merge(const ControlList &other)
{
	// \todo check id map of `other`?

	/* Copy the data and update a temporary state (`newState`) */

	const auto oldState = state_.load(std::memory_order_relaxed);
	auto newState = oldState;
	const auto entries = this->entries();

	for (const auto &[tag, value] : other) {
		auto *e = find(tag);
		if (!e)
			return {};

		auto [ err, header ] = set(*e, value, newState);
		if (err != SetError())
			return {};

		/* HACK: temporarily use the `tag` member to store the entry index */
		header->tag = e - entries.data();
	}

	/*
	 * At this point the data is already in place and every item has been validated
	 * to have a known id, appropriate size and type, etc., but they are not visible
	 * in any way. The next step is to make them visible by updating `headerOffset`
	 * in each affected `Entry` and `state_` in `*this`.
	 */

	iterator it(p_ + contentOffset_ + oldState.fill);
	const iterator end(p_ + contentOffset_ + newState.fill);

	for (; it != end; ++it) {
		auto &header = const_cast<ValueHeader &>(it.header());
		auto &e = entries[header.tag]; /* HACK: header.tag is temporarily the Entry index */

		header.tag = e.tag; /* HACK: restore */

		e.headerOffset.store(
			reinterpret_cast<const std::byte *>(&header) - p_,
			std::memory_order_release
		);
	}

	state_.store(newState, std::memory_order_release);

	return {{ *this, newState.count - oldState.count, oldState.fill, newState.fill }};
}

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

MetadataList::SetError MetadataList::set(Entry &e, ControlValueView v)
{
	auto s = state_.load(std::memory_order_relaxed);

	auto [ err, header ] = set(e, v, s);
	if (err != SetError())
		return err;

	e.headerOffset.store(
		reinterpret_cast<const std::byte *>(header) - p_,
		std::memory_order_release
	);
	state_.store(s, std::memory_order_release);

	return {};
}

std::pair<MetadataList::SetError, MetadataList::ValueHeader *>
MetadataList::set(const Entry &e, ControlValueView v, State &s)
{
	if (e.hasValue())
		return { SetError::AlreadySet, {} };
	if (e.type != v.type() || e.isArray != v.isArray())
		return { SetError::TypeMismatch, {} };

	const auto src = v.data();
	if (e.isArray) {
		if (src.size_bytes() > e.capacity)
			return { SetError::SizeMismatch, {} };
	} else {
		if (src.size_bytes() != e.capacity)
			return { SetError::SizeMismatch, {} };
	}

	std::byte *oldEnd = p_ + contentOffset_ + s.fill;
	std::byte *p = oldEnd;

	auto *headerPtr = internal::align::up<ValueHeader>(p);
	auto *dataPtr = internal::align::up(src.size_bytes(), e.alignment, p);
	internal::align::up(0, alignof(ValueHeader), p);

#if HAS_ASAN
	::__sanitizer_annotate_contiguous_container(
		p_ + contentOffset_, p_ + alloc_,
		oldEnd, p
	);
#endif

	auto *header = new (headerPtr) ValueHeader{
		.tag = e.tag,
		.size = uint32_t(src.size_bytes()),
		.alignment = e.alignment,
		.type = v.type(),
		.isArray = v.isArray(),
		.numElements = uint32_t(v.numElements()),
	};
	std::memcpy(dataPtr, src.data(), src.size_bytes());

	s.fill += p - oldEnd;
	s.count += 1;

	return { {}, header };
}

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
 * \brief Designates a series of consecutively added metadata items
 *
 * A Diff object provides a partial view into a MetadataList, it designates
 * a series of consecutively added metadata items. Its main purposes is to
 * enable applications to receive a list of changes made to a MetadataList.
 *
 * \sa Camera::metadataAvailable
 * \internal
 * \sa MetadataList::Checkpoint::diffSince()
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
 * \fn MetadataList::Diff::get(uint32_t tag) const
 * \copydoc MetadataList::get(uint32_t tag) const
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
 * \brief Designates a particular state of a MetadataList
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
