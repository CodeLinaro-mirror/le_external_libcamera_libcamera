# SPDX-License-Identifier: BSD-2-Clause
#
# Copyright (C) 2019, Raspberry Pi Ltd
# Copyright (C) 2024, Ideas on Board oy
#
# mali-c55.py - LSC module for tuning mali-c55

from .lsc import LSC

import libtuning as lt

import numpy as np

class LSCMaliC55(LSC):
	out_name = 'Lsc'
	name = "LSCMaliC55"

	def __init__(self, *args, **kwargs):
		super().__init__(**kwargs)

	def validate_config(self, config: dict) -> bool:
		# There's nothing we need in config
		return True

	def _do_single_lsc(self, image: lt.Image):
		"""
		Generate lens shading tables of gain values for a single image

		This function generates a set of lens shading tables - one for
		each colour channel. The table values are gain values, expressed
		generically (I.E. not in the IPA module's format)

		Returns a tuple of colour temperature, array of red gain values,
		array of blue gain values, array of red's green gain values and
		array of blue's green gain values.
		"""
		cgr, _ = self._lsc_single_channel(image.channels[lt.Color.GR],
						   image)
		cgb, _ = self._lsc_single_channel(image.channels[lt.Color.GB],
						   image)
		cr, _ = self._lsc_single_channel(image.channels[lt.Color.R],
						 image)
		cb, _ = self._lsc_single_channel(image.channels[lt.Color.B],
						 image)

		return (
			image.color,
			cr.flatten(),
			cb.flatten(),
			cgr.flatten(),
			cgb.flatten()
		)

	def _do_all_lsc(self, images: list) -> dict:
		"""
		Generate lens shading tables in the IPA's format

		This function generates lens shading tables from the list of
		images. A set (one per colour channel) of tables is generated
		for each image, and then the sets from all images which were
		generated from the same colour temperature are averaged together
		and transformed into the format required by the IPA module.

		Returns a list of dictionaries containing colour temperature,
		red lens shading table, green lens shading table and blue lens
		shading table.
		"""

		output_map_func = lt.gradient.Linear().map

		list_ct = []

		list_cr = []
		list_cb = []
		list_cgr = []
		list_cgb = []

		for image in images:
			ct, cr, cg, cgr, cgb = self._do_single_lsc(image)

			list_ct.append(ct)
			list_cr.append(cr)
			list_cb.append(cg)
			list_cgr.append(cgr)
			list_cgb.append(cgb)

		list_ct = np.array(list_ct)
		list_cr = np.array(list_cr)
		list_cb = np.array(list_cb)
		list_cgr = np.array(list_cgr)
		list_cgb = np.array(list_cgb)
		list_cg = (list_cgr + list_cgb) / 2

		# We need to map the gains into the IPA-specific values to pass
		# to the ISP. For the mali-c55 the values are always in the
		# range [0..255] but the min/max that those values represent
		# depend on the mesh scale parameter, so we'll need to choose
		# what that should be and use the gain-range it represents as
		# the domain for output_map_func().
		#
		# For convenient reference, the possible mesh scale values are
		# as follows (taken from include/linux/mali-c55-config.h)
		#
		#	- 0 = 0-2x gain
		#	- 1 = 0-4x gain
		#	- 2 = 0-8x gain
		#	- 3 = 0-16x gain
		#	- 4 = 1-2x gain
		#	- 5 = 1-3x gain
		#	- 6 = 1-5x gain
		#	- 7 = 1-9x gain
		#
		# We want to use the scale with the smallest range that still
		# covers the minimum and maximum value we want to set...but this
		# process at present hard-codes a minimum gain of 1.0, so the
		# first 4 scales are out right away. We'll just consider the
		# minimum as 1.0 for now and if we ever need more than 9.0 gain
		# we'll have to fix this - shout about that if so.

		max_gain = np.max([list_cr, list_cgr, list_cgb, list_cb])
		if (max_gain > 9.0):
			print("WARNING: Maximum gain restricted artificially to 9.0")

		mesh_scales = {
			4: (1.0, 2.0),
			5: (1.0, 3.0),
			6: (1.0, 5.0),
			7: (1.0, 9.0)
		}

		for i in mesh_scales.keys():
			if max_gain <= mesh_scales[i][1]:
				break

		mesh_scale = i

		output_list = []
		for ct in sorted(set(list_ct)):
			indices = np.where(list_ct == ct)
			ct = int(ct)

			tables = []
			for lists in [list_cr, list_cg, list_cb]:
				table = np.mean(lists[indices], axis=0)

				table = output_map_func(
					(
						mesh_scales[mesh_scale][0],
						mesh_scales[mesh_scale][1] - 0.001
					),
					(0, 255),
					table
				)
				table = np.round(table).astype('uint8').tolist()
				tables.append(table)

			entry = {
				'ct': ct,
				'r': tables[0],
				'g': tables[1],
				'b': tables[2],
			}

			output_list.append(entry)

		return {
			'meshScale': mesh_scale,
			'sets': output_list
		}

	def process(self, config: dict, images: list, outputs: dict) -> dict:
		return self._do_all_lsc(images)

