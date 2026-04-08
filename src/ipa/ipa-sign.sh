#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2020, Google Inc.
#
# Author: Laurent Pinchart <laurent.pinchart@ideasonboard.com>
#
# Generate a signature for an IPA module

key="$1"
input="$2"
output="$3"

if openssl pkey -text -noout -in "${key}" 2>/dev/null | grep -q "ML-DSA"; then
	openssl pkeyutl -sign -inkey "${key}" -rawin \
		-in "${input}" -out "${output}"
else
	openssl dgst -sha256 -sign "${key}" -out "${output}" "${input}"
fi
