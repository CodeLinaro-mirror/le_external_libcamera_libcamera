#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2025, Ideas on Board Oy
#
# Author: Laurent Pinchart <laurent.pinchart@ideasonboard.com>
#
# Move Doxygen-generated API documentation to correct location

doc_dir="${MESON_INSTALL_DESTDIR_PREFIX}/$1"
api_dir="$2"

echo "Moving Doxygen ${api_dir} API documentation"

rm -r "${doc_dir}/html/${api_dir}"
mv "${doc_dir}/${api_dir}" "${doc_dir}/html/"
