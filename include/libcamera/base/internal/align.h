/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * Copyright (C) 2025, Ideas on Board Oy
 *
 * Alignment utilities
 */

#pragma once

#include <cassert>
#include <cstddef>
#include <cstdint>

#include <libcamera/base/internal/cxx20.h>

/**
 * \internal
 * \file align.h
 * \brief Utilities for handling alignment
 */

namespace libcamera::internal::align {

/**
 * \internal
 * \brief Check if pointer is aligned
 * \param[in] p pointer to check
 * \param[in] alignment desired alignment
 * \return true if \a p is at least \a alignment aligned, false otherwise
 */
inline bool is(const void *p, std::uintptr_t alignment)
{
	assert(alignment > 0);

	return reinterpret_cast<std::uintptr_t>(p) % alignment == 0;
}

/**
 * \internal
 * \brief Align number up
 * \param[in] x number to check
 * \param[in] alignment desired alignment
 * \return true if \a p is at least \a alignment aligned, false otherwise
 */
template<typename T>
constexpr T up(T x, cxx20::type_identity_t<T> alignment)
{
	static_assert(std::is_unsigned_v<T>);
	assert(alignment > 0);

	const auto padding = (alignment - (x % alignment)) % alignment;
	assert(x + padding >= x);

	return x + padding;
}

/**
 * \internal
 * \brief Align pointer up
 * \param[in] p pointer to align
 * \param[in] alignment desired alignment
 * \return \a p up-aligned to \a alignment
 */
template<typename T>
auto *up(T *p, std::uintptr_t alignment)
{
	using U = std::conditional_t<
		std::is_const_v<T>,
		const std::byte,
		std::byte
	>;

	return reinterpret_cast<U *>(up(reinterpret_cast<std::uintptr_t>(p), alignment));
}

/**
 * \internal
 * \brief Align pointer up
 * \param[in] size required number of bytes
 * \param[in] alignment required alignment
 * \param[in] ptr base pointer
 * \param[in] avail number of available bytes
 *
 * This function checks if the storage starting at \a ptr and continuing for \a avail
 * bytes (if \a avail is nullptr, then no size checking is done) can accommodate \a size
 * bytes of data aligned to \a alignment. If so, then a pointer to the start is the returned
 * and \a ptr is adjusted to point right after the section of \a size bytes. If present,
 * \a avail is also adjusted.
 *
 * Similar to std::align().
 *
 * \return the appropriately aligned pointer, or nullptr if there is not enough space
 */
template<typename T>
T *up(std::size_t size, std::size_t alignment, T *&ptr, std::size_t *avail = nullptr)
{
	assert(alignment > 0);

	auto p = reinterpret_cast<std::uintptr_t>(ptr);
	const auto padding = (alignment - (p % alignment)) % alignment;

	if (avail) {
		if (size > *avail || padding > *avail - size)
			return nullptr;

		*avail -= size + padding;
	}

	p += padding;
	ptr = reinterpret_cast<T *>(p + size);

	return reinterpret_cast<T *>(p);
}

/**
 * \internal
 * \brief Align pointer up
 * \tparam U desired type
 * \param[in] ptr base pointer
 * \param[in] avail number of available bytes
 *
 * A convenience wrapper around libcamera::internal::align::up(std::size_t, std::size_t, T *&, std::size_t *)
 * that takes the size and alignment information from the type \a U.
 *
 * \sa libcamera::internal::align::up(std::size_t, std::size_t, T *&, std::size_t *)
 */
template<typename U, typename T>
U *up(T *&ptr, std::size_t *avail = nullptr)
{
	return reinterpret_cast<std::conditional_t<std::is_const_v<T>, const U, U> *>(
		up(sizeof(U), alignof(U), ptr, avail)
	);
}

} /* namespace libcamera::internal::align */
