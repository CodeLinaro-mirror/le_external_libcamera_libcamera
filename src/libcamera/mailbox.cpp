/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * Copyright (C) 2024, Google Inc.
 *
 * mailbox.cpp - Template class for generic mailbox
 */

#include "libcamera/internal/mailbox.h"

#include <libcamera/base/log.h>

namespace libcamera {

/**
 * \class MailBox
 * \brief MailBox of data
 *
 * A MailBox contains a block of data that has a single producer, and one or
 * multiple consumers. It's often used as a shared_ptr (SharedMailBox) and
 * being held by different tasks in pipelines.
 */

/**
 * \typedef MailBox::Recycler
 * \brief A function that recycles the data when the mailbox is destructed
 */

template<class T>
MailBox<T>::~MailBox()
{
	if (valid_ && recycler_)
		recycler_(item_);
}

/**
 * \brief Set the data as the producer. Should be called only once
 * \param[in] item The data to be stored
 * \param[in] recycler The function that recycles \a data when destructing the
 * mailbox. Mostly used for recycling buffers.
 */
template<class T>
void MailBox<T>::put(const T &item, MailBox<T>::Recycler recycler)
{
	ASSERT(!valid_);

	valid_ = true;
	recycler_ = recycler;
	item_ = item;
}

/**
 * \brief Get the data as a consumer. put() function should be called ahead
 *
 * \return The stored data
 */
template<class T>
const T &MailBox<T>::get()
{
	ASSERT(valid_);
	return item_;
}

/**
 * \fn MailBox::valid()
 * \return True if put() function has been called
 */

/**
 * \typedef SharedMailBox
 * \brief A mailbox as a shared_ptr
 */

/**
 * \fn makeMailBox()
 * \brief A helper function to create a SharedMailBox
 *
 * \return A mailbox as a SharedMailBox
 */

/**
 * \brief A helper function to create a list of mailboxes
 * \param[in] count The number of mailboxes requested
 *
 * \return Mailboxes as a vector
 */
template<class T>
std::vector<SharedMailBox<T>> makeMailBoxVector(const unsigned int count)
{
	std::vector<SharedMailBox<T>> mailBoxes;
	mailBoxes.resize(count);
	for (unsigned int i = 0; i < count; i++)
		mailBoxes[i] = makeMailBox<T>();

	return mailBoxes;
}

} /* namespace libcamera */
