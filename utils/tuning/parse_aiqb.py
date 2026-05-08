#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
#
# Parse an Intel AIQB (CPFF) binary to extract CCMs and AWB chromaticity
# limits for use in a libcamera Simple IPA tuning YAML file.
#
# Tested against OV2740_CJFLE23_ADL.aiqb (Alder Lake, Intel ipu6-camera-hal).
# Other sensors and AIQB versions may require adjustments to the record
# layout assumptions.
#
# AIQB files for Intel IPU6 sensors are available in the ipu6-camera-hal
# repository at config/linux/ipu6ep/. Alternatively, extract from the OEM
# Windows camera driver installer using p7zip and innoextract.
#
# Usage:
#   python3 parse_aiqb.py <sensor>.aiqb [--sensor-name <name>] \
#       [--black-level <value>]
#
# The black level is NOT extracted from the AIQB. Pass --black-level with the
# sensor's black level in 16-bit convention (value >> 8 = 8-bit black level).
# Check the sensor datasheet or kernel driver for the correct value.
# Example: OV2740 has 0x40 at 10-bit (64 ADU), which is 4096 in 16-bit conv.

import argparse
import os
import struct
import sys

# ia_mkn_record_header: size(u32), fmt_id(u8), key_id(u8), name_id(u16)
REC_HDR = struct.Struct('<IBBH')
REC_HDR_SIZE = 8

# cmc_name_id enum values
CMC_GENERAL_DATA       = 2
CMC_SENSITIVITY        = 7
CMC_COLOR_MATRICES     = 18
CMC_ADV_COLOR_MATRICES = 25

# cmc_color_matrix_t (84 bytes):
#   int32 light_src_type
#   uint16 r_per_g, b_per_g
#   uint16 cie_x, cie_y
#   int32 matrix_accurate[9]
#   int32 matrix_preferred[9]
COLOR_MATRIX = struct.Struct('<i HH HH 9i 9i')
assert COLOR_MATRIX.size == 84

# Light source enum -> approximate CCT in Kelvin
LIGHT_SOURCE_CCT = {
    1:  2856,   # A - Incandescent/Tungsten
    4:  5003,   # D50
    5:  5503,   # D55
    6:  6504,   # D65
    7:  7504,   # D75
    8:  5454,   # E (equal energy)
    9:  6430,   # F1 daylight fluorescent
    10: 4230,   # F2 cool white
    11: 3450,   # F3 white
    12: 3000,   # F4 warm white
    13: 6350,   # F5
    14: 4150,   # F6
    15: 6500,   # F7 D65 sim
    16: 5000,   # F8 D50 sim
    17: 4150,   # F9
    18: 5000,   # F10
    19: 4000,   # F11
    20: 3000,   # F12
    22: 2300,   # HZ horizon
}

# Record chain starts here in all AIQB files checked so far
FIRST_RECORD_OFFSET = 0x50

def walk_records(data):
    records = {}
    offset = FIRST_RECORD_OFFSET
    while offset + REC_HDR_SIZE <= len(data):
        size, fmt_id, key_id, name_id = REC_HDR.unpack_from(data, offset)
        if size < REC_HDR_SIZE or offset + size > len(data):
            break
        records[name_id] = (offset, size)
        offset += size
    return records

def extract_general_data(data, offset):
    w, h, bd, co = struct.unpack_from('<HHHH', data, offset + REC_HDR_SIZE)
    return {'width': w, 'height': h, 'bit_depth': bd, 'color_order': co}

def extract_sensitivity(data, offset):
    iso, = struct.unpack_from('<H', data, offset + REC_HDR_SIZE)
    return iso

def extract_color_matrices(data, offset):
    num, = struct.unpack_from('<H', data, offset + REC_HDR_SIZE)
    matrices = []
    mat_offset = offset + REC_HDR_SIZE + 2
    for i in range(num):
        fields = COLOR_MATRIX.unpack_from(data, mat_offset + i * 84)
        light_src = fields[0]
        matrices.append({
            'light_src': light_src,
            'cct': LIGHT_SOURCE_CCT.get(light_src),
            'r_per_g_raw': fields[1],
            'b_per_g_raw': fields[2],
            'matrix_raw': fields[5:14],
        })
    return matrices

def extract_advanced_color_matrices(data, offset):
    """Parse cmc_advanced_color_matrix_correction (record id=25).

    Layout after the 8-byte record header:
      uint16  num_light_srcs
      uint16  num_sectors
      uint32  hue_of_sectors[num_sectors]
      Per light source (24-byte cmc_acm_color_matrices_info_v101_t):
        uint32  src_type
        float   r_per_g
        float   b_per_g
        float   cie_x
        float   cie_y
        uint32  cct              (Kelvin, directly)
        float   traditional[9]   (3x3 CCM, rows sum to 1.0)
        float   advanced[num_sectors][9]   (per-sector CCMs, skipped)
    """
    pos = offset + REC_HDR_SIZE
    num_ls, num_sectors = struct.unpack_from('<HH', data, pos)
    pos += 4
    pos += num_sectors * 4  # skip hue_of_sectors

    INFO = struct.Struct('<IffffI')  # 24 bytes
    CCM  = struct.Struct('<9f')     # 36 bytes

    matrices = []
    for _ in range(num_ls):
        src_type, rg, bg, cie_x, cie_y, cct_k = INFO.unpack_from(data, pos)
        pos += 24
        trad = CCM.unpack_from(data, pos)
        pos += 36
        pos += num_sectors * 36  # skip per-sector advanced CCMs
        matrices.append({
            'light_src': src_type,
            'cct': cct_k,
            'r_per_g': rg,
            'b_per_g': bg,
            'matrix_float': trad,
        })
    return matrices

def guess_ccm_scale(matrices):
    for scale in (8192, 4096, 2048, 1024):
        errors = []
        for m in matrices:
            for row in range(3):
                s = sum(m['matrix_raw'][row * 3:(row + 1) * 3]) / scale
                errors.append(abs(s - 1.0))
        if max(errors) < 0.05:
            return scale
    return 8192

def format_ccm(vals):
    rows = []
    for row in range(3):
        r = vals[row * 3:(row + 1) * 3]
        rows.append(f"          {r[0]:8.4f}, {r[1]:8.4f}, {r[2]:8.4f}")
    return '[\n' + ',\n'.join(rows) + ' ]'

def main():
    parser = argparse.ArgumentParser(
        description='Parse Intel AIQB binary for libcamera Simple IPA YAML',
        epilog='Tested on OV2740_CJFLE23_ADL.aiqb only. Other sensors may '
               'require adjustments.')
    parser.add_argument('aiqb', help='Path to .aiqb file')
    parser.add_argument('--sensor-name',
                        help='Sensor name for YAML output (default: derived '
                             'from filename)')
    parser.add_argument('--black-level', type=int, default=0,
                        help='Black level in 16-bit convention, e.g. 4096 for '
                             'OV2740 (default: 0 = unknown, emits placeholder)')
    parser.add_argument('--ccm-scale', type=int, default=0,
                        help='Integer CCM scale for record id=18 (0=autodetect)')
    args = parser.parse_args()

    sensor_name = args.sensor_name or os.path.basename(args.aiqb).split('_')[0].lower()

    with open(args.aiqb, 'rb') as f:
        data = f.read()

    print(f"File: {args.aiqb} ({len(data)} bytes)")

    records = walk_records(data)
    print(f"Records found: {sorted(records.keys())}\n")

    if CMC_GENERAL_DATA in records:
        gd = extract_general_data(data, records[CMC_GENERAL_DATA][0])
        print(f"Sensor: {gd['width']}x{gd['height']}, {gd['bit_depth']}-bit, "
              f"color_order={gd['color_order']}")

    if CMC_SENSITIVITY in records:
        iso = extract_sensitivity(data, records[CMC_SENSITIVITY][0])
        print(f"Base ISO: {iso}")

    adv_mode = False
    if CMC_ADV_COLOR_MATRICES in records:
        matrices = extract_advanced_color_matrices(data, records[CMC_ADV_COLOR_MATRICES][0])
        adv_mode = True
        print(f"\nAdvanced color matrices (id=25, {len(matrices)} entries, float CCMs):")
    elif CMC_COLOR_MATRICES in records:
        matrices = extract_color_matrices(data, records[CMC_COLOR_MATRICES][0])
        print(f"\nColor matrices (id=18, {len(matrices)} entries):")
    else:
        print("ERROR: no color_matrices record found (id=18 or id=25)")
        sys.exit(1)

    ccm_scale = args.ccm_scale or (1 if adv_mode else guess_ccm_scale(matrices))

    valid_matrices = []
    for m in matrices:
        cct = m['cct']
        if not cct:
            continue
        if adv_mode:
            vals = list(m['matrix_float'])
            rg = m['r_per_g']
            bg = m['b_per_g']
        else:
            vals = [v / ccm_scale for v in m['matrix_raw']]
            rg = m['r_per_g_raw'] / 256.0
            bg = m['b_per_g_raw'] / 256.0
        row_sums = [sum(vals[r * 3:(r + 1) * 3]) for r in range(3)]
        if max(abs(s - 1.0) for s in row_sums) > 0.05:
            print(f"  WARNING: CCT={cct}K row sums {[round(s, 4) for s in row_sums]}")
        print(f"  CCT={cct}K  R/G={rg:.4f}  B/G={bg:.4f}")
        valid_matrices.append((cct, vals, rg, bg))

    max_gain_r = max_gain_b = None
    if valid_matrices:
        min_rg = min(m[2] for m in valid_matrices)
        min_bg = min(m[3] for m in valid_matrices)
        max_gain_r = round((1.0 / min_rg) * 1.1, 2) if min_rg > 0 else 2.5
        max_gain_b = round((1.0 / min_bg) * 1.1, 2) if min_bg > 0 else 3.2
        print(f"\nSuggested AWB maxGainR={max_gain_r}, maxGainB={max_gain_b} "
              f"(from min R/G={min_rg:.4f}, min B/G={min_bg:.4f})")

    aiqb_name = os.path.basename(args.aiqb)
    print("\n" + "=" * 60)
    print(f"# {sensor_name}.yaml for libcamera Simple IPA")
    print("# SPDX-License-Identifier: CC0-1.0")
    print(f"# Calibrated from {aiqb_name}")
    print("%YAML 1.1")
    print("---")
    print("version: 1")
    print("algorithms:")
    print("  - BlackLevel:")
    if args.black_level:
        print(f"      blackLevel: {args.black_level}")
    else:
        print("      blackLevel: 0  # TODO: set correct value from sensor datasheet")
    print("  - Awb:")
    print(f"      maxGainR: {max_gain_r}")
    print(f"      maxGainB: {max_gain_b}")
    print("      speed: 0.25")
    print("  - Ccm:")
    print("      ccms:")
    for cct, vals, rg, bg in sorted(valid_matrices):
        print(f"        - ct: {cct}")
        print(f"          ccm: {format_ccm(vals)}")
    print("  - Adjust:")
    print("      gamma: 2.2")
    print("      contrast: 1.0")
    print("      saturation: 1.0")
    print("  - Agc:")
    print("...")

if __name__ == '__main__':
    main()
