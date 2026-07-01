#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2020, Google Inc.
#
# Author: Laurent Pinchart <laurent.pinchart@ideasonboard.com>
#
# Generate an private key for the given algorithm to sign IPA modules

algo="$1"
key="$2"

if [ -e "${key}" ]; then
    echo "File ${key} already exists. Removing it."
    rm "${key}"
fi

# Two possible algorithms: RSA and ML-DSA-65

if [ "$algo" = "rsa-sha256" ]; then
    openssl genpkey -algorithm RSA -out "${key}"
elif [ "$algo" = "ml-dsa-65" ]; then
    openssl genpkey -algorithm ML-DSA-65 -out "${key}"
else
    echo "Invalid algorithm: $algo"
    exit 1
fi
