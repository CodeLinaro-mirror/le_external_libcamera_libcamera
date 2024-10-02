#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2024, Ideas on Board Inc.
#
# Author: Stefan Klug <stefan.klug@ideasonboard.com>
#
# This script looks for occurrences of the debug metadata controls in the source
# tree and updates src/libcamera/control_ids_debug.yaml accordingly

import argparse
import logging
import os
import re
import sys
from dataclasses import dataclass
from pathlib import Path

logger = logging.getLogger(__name__)
logging.basicConfig(level=logging.INFO, format='%(levelname)s: %(message)s')

try:
    import ruyaml
except:
    logger.error(
        f'Failed to import ruyaml. Please install the ruyaml package.')
    sys.exit(1)


@dataclass
class FoundMatch:
    file: os.PathLike
    whole_match: str
    line: int
    type: str
    name: str
    size: str = None


def get_control_name(control):
    k = list(control.keys())
    if len(k) != 1:
        raise Exception(f"Can't handle control entry with {len(k)} keys")
    return k[0]


def find_debug_controls(dir):
    extensions = ['.cpp', '.h']
    files = [p for p in dir.rglob('*') if p.suffix in extensions]

    # The following regex was tested on
    # set<Span<type>>( controls::debug::something , static_cast<type>(var) )
    # set<>( controls::debug::something , static_cast<type>(var) )
    # set( controls::debug::something , static_cast<type> (var) )
    exp = re.compile(r'set'  # set function
                     # possibly followed by template param
                     r'(?:\<((?:[^)(])*)\>)?'
                     # referencing a debug control
                     r'\(\s*controls::debug::(\w+)\s*,.*\)'
                     )
    matches = []
    for p in files:
        with p.open('r') as f:
            for idx, line in enumerate(f):
                match = exp.search(line)
                if match:
                    m = FoundMatch(file=p, line=idx, type=match.group(1),
                                   name=match.group(2), whole_match=match.group(0))
                    if m.type is not None and m.type.startswith('Span'):
                        # simple span type detection treating the last word inside <> as type
                        r = re.match(r'Span<(?:.*\s+)(.*)>', m.type)
                        m.type = r.group(1)
                        m.size = '[n]'
                    matches.append(m)
    return matches


def main(argv):
    parser = argparse.ArgumentParser(
        description='Automatically updates control_ids_debug.yaml',)
    args = parser.parse_args(argv[1:])

    yaml = ruyaml.YAML()
    root_dir = Path(__file__).resolve().parent.parent
    output = root_dir.joinpath('src/libcamera/control_ids_debug.yaml')

    matches = find_debug_controls(root_dir.joinpath('src'))

    doc = yaml.load(output)

    controls = doc['controls']

    # create a map of names in the existing yaml for easier updating
    controls_map = {}
    for control in controls:
        for k, v in control.items():
            controls_map[k] = v

    obsolete_names = list(controls_map.keys())

    for m in matches:
        if not m.type:
            p = m.file.relative_to(Path.cwd(), walk_up=True)
            logger.warning(
                f'{p}:{m.line + 1}: Failed to deduce type from {m.whole_match} ... skipping')
            continue

        p = m.file.relative_to(root_dir)
        desc = {'type': m.type,
                'description': f'Debug control {m.name} found in {p}:{m.line}'}
        if m.size is not None:
            desc['size'] = m.size

        if m.name in controls_map:
            # Can't use == for modified check because of the special yaml dicts.
            update_needed = False
            if list(controls_map[m.name].keys()) != list(desc.keys()):
                update_needed = True
            else:
                for k, v in controls_map[m.name].items():
                    if v != desc[k]:
                        update_needed = True
                        break

            if update_needed:
                logger.info(f"Update control '{m.name}'")
                controls_map[m.name].clear()
                controls_map[m.name].update(desc)

            obsolete_names.remove(m.name)
        else:
            logger.info(f"Add control '{m.name}'")
            insert_before = len(controls)
            for idx, control in enumerate(controls):
                if get_control_name(control).lower() > m.name.lower():
                    insert_before = idx
                    break
            controls.insert(insert_before, {m.name: desc})

    # Remove elements from controls without recreating the list (to keep comments etc.)
    idx = 0
    while idx < len(controls):
        name = get_control_name(controls[idx])
        if name in obsolete_names:
            logger.info(f"Remove control '{name}'")
            controls.pop(idx)
        else:
            idx += 1

    with output.open('w') as f:
        # ruyaml looses the copyright header
        f.write(("# SPDX-License-Identifier: LGPL-2.1-or-later\n"
                 "#\n"
                 "# Copyright (C) 2024, Ideas on Board Oy\n"
                 "#\n"))
        yaml.dump(doc, f)

    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
