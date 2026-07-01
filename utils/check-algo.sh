#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026, Red Hat Inc.
#
# Author: Kate Hsuan <hpa@redhat.com>
#
# Check if the given algorithm is supported by the library

tool="$1"
algo="$2"

if [ "$algo" = "rsa-sha256" ]; then
    algo="RSA-SHA256"
elif [ "$algo" = "ml-dsa-65" ]; then
    algo="ML-DSA-65"
else
    echo "Invalid algorithm: $algo"
    exit 1
fi

if [ "$tool" = "guntls" ]; then
    gnutls-cli -l | grep "$algo" > /dev/null
    ret=$?
elif [ "$tool" = "openssl" ]; then
    openssl list -signature-algorithms |grep ${algo} > /dev/null
    ret=$?
else
    echo "Invalid tool: $tool"
    exit 1
fi

if [ $ret -ne 0 ]; then
    echo "Algorithm $algo is not supported by $tool"
    exit 1
fi
