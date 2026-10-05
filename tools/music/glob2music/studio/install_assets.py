# SPDX-License-Identifier: GPL-3.0-or-later
"""Image-build-only provisioning of pinned acoustic and synth assets."""
import argparse
import hashlib
from pathlib import Path
from ..backends.sfizz import SfizzBackend, read_lock, write_lock, library_root
from ..backends.surge import surge_vst3
from .palettes import SETS, instruments


def install(cache):
    cache.mkdir(parents=True, exist_ok=True)
    entries = {}
    for name in ('moss-lanterns','thistle-waltz','bramble-jig','fennel-mist'):
        for entry in read_lock(SETS / name / 'samples.lock'):
            key = (entry.library,entry.path)
            if key in entries and entries[key] != entry: raise ValueError('Conflicting sample pins')
            entries[key] = entry
    lock = cache / 'samples.lock'
    write_lock(lock,list(entries.values()))
    backend = SfizzBackend(lock,cache,concurrency=2)
    for entry in entries.values():
        path=library_root(entry.library,cache)/entry.path
        if hashlib.sha256(path.read_bytes()).hexdigest()!=entry.sha256:
            raise ValueError(f'Pinned sample changed: {entry.library}/{entry.path}')
    for key in instruments('acoustic-v1'):
        backend.sfz_path(key)  # Generate RAM wrappers before the cache becomes read-only.
    surge_vst3(cache)


if __name__ == '__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('cache',type=Path)
    install(parser.parse_args().cache)
