#!/bin/sh

if [ $# -lt 4 ]; then
	echo "Invalid arg count must be >= 5"
	exit 1
fi
src_dir="$1"; shift
dst_dir="$1"; shift
dst_path=$dst_dir/"$1"; shift

cat <<EOF > "$dst_path"
/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* This file is auto-generated, do not edit! */
/*
 * Copyright (C) 2024, Linaro Ltd.
 *
 */

#pragma once

EOF

cat <<EOF >> "$dst_path"
/*
 * List the names of the shaders at the top of
 * header for readability's sake
 *
EOF
for file in $@; do
	name=`basename $src_dir/src/$file | tr '.' '_'`
	echo " * unsigned char $name;" >> $dst_path
done

echo "*/" >> $dst_path

echo "/* Hex encoded shader data */" >> $dst_path
for file in $@; do
	name=`basename $src_dir/src/$file`
	$src_dir/utils/gen-shader-header.py $name $src_dir/src/$file >> $dst_path
	echo >> $dst_path
done
