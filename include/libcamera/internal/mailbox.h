/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * Copyright (C) 2024, Google Inc.
 *
 * mailbox.h - Template class for generic mailbox
 */

#pragma once

#include <functional>
#include <memory>
#include <vector>

namespace libcamera {

template<class T>
class MailBox
{
public:
	using Recycler = std::function<void(T &)>;

	MailBox()
		: valid_(false) {}
	~MailBox();

	void put(const T &item, Recycler recycler);

	const T &get();

	bool valid() { return valid_; }

private:
	T item_;
	bool valid_;
	std::function<void(T &)> recycler_;
};

template<class T>
using SharedMailBox = std::shared_ptr<MailBox<T>>;

template<class T>
SharedMailBox<T> makeMailBox()
{
	return std::make_shared<MailBox<T>>();
}

template<class T>
std::vector<SharedMailBox<T>> makeMailBoxVector(const unsigned int count);

} /* namespace libcamera */
