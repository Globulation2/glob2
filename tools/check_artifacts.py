#!/usr/bin/env python3
"""Find what makes artifacts/ directories grow without bound, and optionally fix it.

    python3 tools/check_artifacts.py [ROOT ...]          report
    python3 tools/check_artifacts.py --fix [ROOT ...]    gzip large logs, convert .ppm/.bmp to .png

Reported problems: uncompressed .ppm/.bmp images (PNG is lossless and ~10x
smaller), whole build trees copied under artifacts/ (an identity.json marks one),
and uncompressed logs over 1 MiB. --fix never deletes a build tree; remove those
by hand once nothing needs them. Defaults to this checkout's artifacts/.
"""
from __future__ import annotations

import argparse
import gzip
import os
import shutil
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
LOG_LIMIT = 1 << 20
LOG_SUFFIXES = {'.log', '.txt', '.trace', '.out'}
RAW_IMAGES = {'.ppm', '.bmp'}


def scan(root: Path):
    for directory, names, files in os.walk(root):
        here = Path(directory)
        if 'identity.json' in files and here != root:
            yield 'build-tree', here
            names[:] = []
            continue
        for name in files:
            path = here / name
            suffix = path.suffix.lower()
            try:
                size = path.stat().st_size
            except OSError:
                continue
            if suffix in RAW_IMAGES:
                yield 'raw-image', path
            elif suffix in LOG_SUFFIXES and size > LOG_LIMIT:
                yield 'large-log', path


def fix(kind: str, path: Path) -> str | None:
    if kind == 'large-log':
        target = path.with_name(path.name + '.gz')
        with path.open('rb') as source, gzip.open(target, 'wb') as output:
            shutil.copyfileobj(source, output)
        path.unlink()
        return f'compressed {path} -> {target.name}'
    if kind == 'raw-image':
        try:
            from PIL import Image
        except ImportError:
            return None
        target = path.with_suffix('.png')
        with Image.open(path) as image:
            image.save(target, optimize=True)
        path.unlink()
        return f'converted {path} -> {target.name}'
    return None


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('roots', nargs='*', type=Path, default=[ROOT / 'artifacts'])
    parser.add_argument('--fix', action='store_true')
    args = parser.parse_args(argv)
    totals: dict[str, list[int]] = {}
    for root in args.roots:
        if not root.is_dir():
            continue
        for kind, path in scan(root):
            size = sum(p.stat().st_size for p in path.rglob('*') if p.is_file()) if path.is_dir() else path.stat().st_size
            entry = totals.setdefault(kind, [0, 0])
            entry[0] += 1
            entry[1] += size
            message = fix(kind, path) if args.fix else None
            print(message or f'{kind:10s} {size / 2**20:9.1f} MiB  {path}')
    for kind, (count, size) in sorted(totals.items()):
        print(f'{kind}: {count} item(s), {size / 2**30:.2f} GiB', file=sys.stderr)
    return 1 if totals and not args.fix else 0


if __name__ == '__main__':
    raise SystemExit(main())
