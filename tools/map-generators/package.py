#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Freeze an authored directory into a portable generator JSON package.

This checks container size and paths. The engine validates the manifest's control
contracts and executes module inspection when loading the resulting package.
"""
import argparse
import json
from pathlib import Path

PACKAGE_BYTES = 4 * 1024 * 1024
MODULE_COUNT = 128


def pack(directory):
    directory = Path(directory)
    if directory.is_symlink():
        raise ValueError('Package directories cannot contain symlinks')
    root = directory.resolve(strict=True)
    if not root.is_dir():
        raise ValueError('Expected an authoring directory')
    manifest_path = root / 'manifest.json'
    if manifest_path.is_symlink():
        raise ValueError('Package directories cannot contain symlinks')
    total = manifest_path.stat().st_size
    if total > PACKAGE_BYTES:
        raise ValueError('Package exceeds runtime limits')
    manifest = json.loads(manifest_path.read_text(encoding='utf-8'))
    if not isinstance(manifest, dict):
        raise ValueError('Manifest must be an object')
    modules = {}
    for path in sorted(root.rglob('*')):
        if path.is_symlink():
            raise ValueError('Package directories cannot contain symlinks')
        if path.is_file() and path.suffix == '.js':
            total += path.stat().st_size
            if total > PACKAGE_BYTES or len(modules) >= MODULE_COUNT:
                raise ValueError('Package exceeds runtime limits')
            # Module bytes participate in package identity. Preserve line endings
            # exactly, matching the native authoring-directory loader.
            modules[path.relative_to(root).as_posix()] = path.read_bytes().decode('utf-8')
    if manifest.get('entry', 'generator.js') not in modules:
        raise ValueError('Missing entry module')
    result = dict(formatVersion=1, manifest=manifest, modules=modules)
    data = json.dumps(result, ensure_ascii=False, sort_keys=True, separators=(',', ':')) + '\n'
    # JSON escaping can make the portable form larger than its source files.
    if len(data.encode('utf-8')) > PACKAGE_BYTES:
        raise ValueError('Package exceeds runtime limits')
    return data


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('directory')
    parser.add_argument('output')
    args = parser.parse_args()
    try:
        Path(args.output).write_text(pack(args.directory), encoding='utf-8')
    except (OSError, ValueError) as error:
        parser.error(str(error))


if __name__ == '__main__':
    main()
