#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2020, Google Inc.
#
# Author: Laurent Pinchart <laurent.pinchart@ideasonboard.com>
#
# Generate an RSA private key to sign IPA modules

algo="$1"
key="$2"

# Two possible algorithms: RSA and ML-DSA-65
# openssl genpkey -algorithm RSA -out "${key}" -pkeyopt rsa_keygen_bits:2048
# openssl genpkey -algorithm ML-DSA-65 -out "${key}"

if [ "$algo" == "RSA" ]; then
    openssl genpkey -algorithm RSA -out "${key}" -pkeyopt rsa_keygen_bits:2048
elif [ "$algo" == "ML-DSA-65" ]; then
    openssl genpkey -algorithm ML-DSA-65 -out "${key}"
else
    echo "Invalid algorithm: $algo"
    exit 1
fi