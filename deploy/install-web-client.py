#!/usr/bin/env python3
"""Copy a built browser client into the directory Caddy serves at /play/.

    deploy/install-web-client.py <release-dir> <served-dir>

Content-addressed data packages (assets/*.data) are added first; then each entry
file is replaced by rename together with its precompressed copies, and index.html
goes last, after the files it loads. Caddy bind-mounts the served directory
itself, so files are swapped in place rather than swapping the directory. Data
packages of the previous installation stay for pages that are still loading it;
older ones are removed.
"""
import json
import os
from pathlib import Path
import shutil
import sys

ENTRY_FILES = ('index.wasm', 'index.js')
ENCODINGS = ('', '.br', '.gz')
RECORD = '.installed-assets.json'


def copy(source, target):
    temporary = target.with_name('.' + target.name + '.new')
    shutil.copyfile(source, temporary)
    os.replace(temporary, target)


def main():
    if len(sys.argv) != 3:
        sys.exit(__doc__.strip().splitlines()[2].strip())
    release, served = Path(sys.argv[1]), Path(sys.argv[2])
    (served / 'assets').mkdir(parents=True, exist_ok=True)
    current = sorted(p.name for p in (release / 'assets').iterdir()
                     if p.is_file() and not p.name.startswith('.'))
    for name in current:
        target = served / 'assets' / name
        if not target.is_file() or target.stat().st_size != (release / 'assets' / name).stat().st_size:
            copy(release / 'assets' / name, target)
    for name in ENTRY_FILES + ('index.html',):
        for suffix in ENCODINGS[1:]:
            if not (release / (name + suffix)).is_file():
                (served / (name + suffix)).unlink(missing_ok=True)
        # Precompressed copies first: Caddy prefers them, so a stale one would win.
        for suffix in sorted(ENCODINGS, reverse=True):
            if (release / (name + suffix)).is_file():
                copy(release / (name + suffix), served / (name + suffix))
    # Earlier builds shipped one data file without compression.
    for suffix in ENCODINGS:
        (served / ('index.data' + suffix)).unlink(missing_ok=True)
    record = served / RECORD
    try:
        previous = set(json.loads(record.read_text()))
    except (OSError, ValueError):
        previous = set()
    keep = set(current) | previous
    for stale in (served / 'assets').iterdir():
        if stale.is_file() and stale.name not in keep:
            stale.unlink()
    record.write_text(json.dumps(current) + '\n')
    print(f'web client: {served} ({len(current)} asset files)')


if __name__ == '__main__':
    main()
