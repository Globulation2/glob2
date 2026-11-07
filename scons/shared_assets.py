"""Share exported runtime-asset trees between checkouts.

Exporting the runtime assets (about 600 MB in 62,000 files) costs minutes of
CPU per fresh worktree, yet every checkout of the same content produces the same
tree. The development store keeps one finished export per input key under
`caches/runtime-assets/<key>/` (`tree/`, `audit.json`, `.complete`); a build
whose key is already there clones it into its build directory instead of
exporting. Clones are copy-on-write where the filesystem supports it (APFS
clonefile, XFS/Btrfs reflinks) and plain copies elsewhere, never hard links, so
a build can never modify a stored tree.

The key covers the exported files' content (Git blob ids, plus content hashes
for modified or untracked files), the export tools and the export options.
"""
from __future__ import annotations

import hashlib
import json
import os
import platform as host_platform
import shutil
import subprocess
import sys
from pathlib import Path

STORE = 'runtime-assets'


def _git(root: Path, *args: str) -> bytes:
    return subprocess.run(['git', '-C', str(root), *args], capture_output=True, check=True).stdout


def content_key(root, paths, extra) -> str:
    """A key for the content of `paths` (files under root) and the `extra` options."""
    root = Path(root).resolve()
    relative = sorted({str(Path(path).resolve().relative_to(root)) for path in paths})
    digest = hashlib.sha256(json.dumps(extra, sort_keys=True).encode())
    try:
        staged = {}
        for record in _git(root, 'ls-files', '-s', '-z').split(b'\0'):
            if record:
                metadata, name = record.split(b'\t', 1)
                staged[name.decode()] = metadata.split()[1].decode()
        modified = {name.decode() for name in _git(root, 'diff-files', '--name-only', '-z').split(b'\0') if name}
    except (OSError, subprocess.CalledProcessError):
        staged, modified = {}, set()
    for name in relative:
        if name in staged and name not in modified:
            identity = 'blob ' + staged[name]
        else:
            identity = 'sha256 ' + hashlib.sha256((root / name).read_bytes()).hexdigest()
        digest.update(f'{name}\0{identity}\0'.encode())
    return digest.hexdigest()


def clone_tree(source: Path, destination: Path) -> None:
    """Copy a tree, sharing data blocks where the filesystem allows."""
    if destination.exists():
        shutil.rmtree(destination)
    destination.parent.mkdir(parents=True, exist_ok=True)
    if host_platform.system() == 'Darwin':
        command = ['cp', '-cR', str(source), str(destination)]
    elif os.name != 'nt':
        command = ['cp', '-a', '--reflink=auto', str(source), str(destination)]
    else:
        command = None
    if command is None or subprocess.run(command, capture_output=True).returncode != 0:
        if destination.exists():
            shutil.rmtree(destination)
        shutil.copytree(source, destination, symlinks=True)


def _store(root):
    here = str(Path(__file__).resolve().parent)
    if here not in sys.path:
        sys.path.append(here)
    import dev_store
    return dev_store, dev_store.cache(root, STORE)


def export(root, output, key: str, produce) -> bool:
    """Fill `output` (and its sibling audit JSON) from the store, or produce and publish it.

    `produce(output)` performs the real export. Returns True on a store hit."""
    output = Path(output)
    audit = output.with_suffix('.json')
    dev_store, store = _store(root)
    entry = store / key
    if (entry / '.complete').is_file():
        with dev_store.Lease(entry):
            clone_tree(entry / 'tree', output)
            shutil.copy2(entry / 'audit.json', audit)
        return True
    produce(output)
    try:
        with dev_store.Lease(entry, exclusive=True):
            if not (entry / '.complete').is_file():
                staging = store / f'.{key}.{os.getpid()}'
                if staging.exists():
                    shutil.rmtree(staging)
                clone_tree(output, staging / 'tree')
                shutil.copy2(audit, staging / 'audit.json')
                if entry.exists():
                    shutil.rmtree(entry)
                staging.rename(entry)
                (entry / '.complete').touch()
    except OSError as error:
        # Publication is an optimization; the build already has its export.
        print(f'warning: could not share the runtime-asset export: {error}', file=sys.stderr)
    return False
