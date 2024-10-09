/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * Copyright (C) 2024, Google Inc.
 *
 * mailbox.cpp - Template class for generic mailbox
 */

/**
 * \class libcamera::MailBox
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

/**
 * \fn MailBox::put(const T &item, MailBox<T>::Recycler recycler)
 * \brief Set the data as the producer. Should be called only once
 * \param[in] item The data to be stored
 * \param[in] recycler The function that recycles \a data when destructing the
 * mailbox. Mostly used for recycling buffers.
 */

/**
 * \fn MailBox::get()
 * \brief Get the data as a consumer. put() function should be called ahead
 *
 * \return The stored data
 */

/**
 * \fn MailBox::valid()
 * \return True if put() function has been called
 */

/**
 * \typedef libcamera::SharedMailBox
 * \brief A mailbox as a shared_ptr
 */

/**
 * \fn libcamera::makeMailBox()
 * \brief A helper function to create a SharedMailBox
 *
 * \return A mailbox as a SharedMailBox
 */

/**
 * \fn libcamera::makeMailBoxVector(const unsigned int count)
 * \brief A helper function to create a list of mailboxes
 * \param[in] count The number of mailboxes requested
 *
 * \return Mailboxes as a vector
 */
