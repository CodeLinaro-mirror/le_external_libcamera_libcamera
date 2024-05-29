#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2024, Ideas on Board Oy
#
# Author: Stefan Klug <klug.stefan@ideasonboard.com>
#
# Validate tuning files against schema

import argparse
import logging
import jsonschema
import yaml
import sys
import json
import os
from pathlib import Path

try:
    import coloredlogs
    coloredlogs.install(level=logging.INFO, fmt='%(levelname)s: %(message)s')
except ImportError:
    logging.basicConfig(level=logging.INFO,
                        format="%(levelname)s: %(message)s")

logger = logging.getLogger()


def do_schema_check(schema_file, files):
    res = True
    with open(schema_file, 'r') as file:
        schema = yaml.safe_load(file)

    for file in files:
        with open(file, 'r') as f:
            logger.info(f"Validating file {file} against {schema_file}")
            data = yaml.safe_load(f)
            try:
                jsonschema.validate(instance=data, schema=schema)
            except jsonschema.exceptions.ValidationError as e:
                logging.error(f"Validation error in file {file}: {e}")
                res = False
    return res

# Checks for the given ipa. If files is None, all files for that ipa get checked


def do_schema_check_ipa(ipa, files=None):
    res = True

    if ipa != 'rkisp1':
        raise ValueError(f"Ipa '{ipa}' is not supported")

    logger.info(f"Checking ipa {ipa}")

    top_dir = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    # Todo implement for other pipelines
    data_dir = 'src/ipa/rkisp1/data'

    full_dir = Path(os.path.join(top_dir, data_dir))

    schema_path = os.path.join(full_dir, f'tuning-{ipa}.schema.yaml')
    if not os.path.isfile(schema_path):
        raise ValueError(f"Schema file {schema_path} doesn't exist")

    if files is None:
        files = [f for f in full_dir.glob(
            '*.yaml') if f.is_file() and not f.name.endswith('.schema.yaml')]

    if not do_schema_check(schema_path, files):
        res = False

    return res


def do_schema_check_all():
    # We only support rkisp1 for now
    return do_schema_check_ipa('rkisp1')


def main():

    parser = argparse.ArgumentParser()
    group = parser.add_mutually_exclusive_group(required=True)
    group.add_argument('--all', '-a', action='store_true',
                       help='automatically find and check all tuning files.')
    group.add_argument('--schema', '-s',
                       help='schema file to check against')
    group.add_argument('--pipeline', '-p',
                       help='pipeline to check against (Currently only rkisp1 is supported)')
    parser.add_argument('files', metavar='FILE', nargs='*',
                        help='tuning files to check')

    args = parser.parse_args()

    if args.all:
        if args.files != []:
            parser.error("No files should be given when using --all")

        if not do_schema_check_all():
            return 1
        return 0

    if not args.files:
        argparse.error("No files to check")
        return 1

    if not do_schema_check(args.schema, args.files):
        return 1


if __name__ == '__main__':
    sys.exit(main())
