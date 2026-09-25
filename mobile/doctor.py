#!/usr/bin/env python3
"""Read-only check of a mobile compiler and its pinned SDK."""
import argparse
import json
from pathlib import Path
import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'scons'))
from build_layout import build_identity, default_directory
from mobile_toolchain import discover


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('target', choices=['android', 'ios'])
    parser.add_argument('options', nargs='*', help='SCons-style key=value options')
    args = parser.parse_args()
    options = {'target': args.target}
    try:
        for option in args.options:
            key, value = option.split('=', 1)
            if key == 'target':
                raise ValueError('Specify target only as the first argument')
            options[key] = value
        identity = build_identity(options)
        toolchain = discover(identity, options)
        print(json.dumps({'identity': identity, 'output': str(default_directory(identity)), 'toolchain': toolchain}, indent=2))
    except ValueError as error:
        print(str(error), file=sys.stderr)
        return 1
    return 0

if __name__ == '__main__':
    sys.exit(main())
