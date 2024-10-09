/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * Copyright (C) 2024, Google Inc.
 *
 * pool.cpp - Template class for generic pool
 */

/**
 * \class libcamera::Pool
 * \brief Pool that owns a list of objects
 *
 * A Pool that owns a list of objects, and allows users to request objects and
 * recycle them. It's mostly used as a buffer pool, holding a list of buffers
 * as std::unique_ptr.
 */

/**
 * \fn Pool::setData(std::vector<UniquePtr> &pool)
 * \brief Set and claim ownership of objects. Swap the previously owned objects
 * not empty.
 * \param[inout] pool The objects to be set and the swapped out ones
 */

/**
 * \fn Pool::release()
 * \brief Release the ownership of objects and reset states
 */

/**
 * \fn Pool::content()
 * \return The reference of objects
 */

/**
 * \fn Pool::size()
 * \brief The size of objects
 */

/**
 * \fn Pool::get()
 * \brief Get one object. Fatal if there's none available
 *
 * \return An available object
 */

/**
 * \fn Pool::put(T data)
 * \brief Recycle one object that was retrieved by get()
 * \param[in] data The object to be recycled
 */

/**
 * \class libcamera::BasicContainer
 * \brief A container for basic types
 *
 * When using Pool with a basic type (e.g. `int`), it'd be easier to get an
 * object by value instead of by pointer. BasicContainer makes it possible as a
 * helper class.
 */

/**
 * \fn BasicContainer::BasicContainer(T &value)
 * \brief Create a BasicContainer with a value
 * \param[in] value The value to be contained
 */

/**
 * \fn BasicContainer::get()
 * \return Return the object by value
 */
