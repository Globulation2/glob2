#!/usr/bin/env python3
"""Print the sim version key (<VERSION_MINOR>-<NET_PROTOCOL_VERSION>-<data hash>) of a source tree.

The engine-agent image is labelled with this key and its agent serves exactly
this sim version. The data hash follows the engine's simDataHash() in
src/online/SimVersion.cpp: SHA-256 over the pseudo-file "#sim-revision" (content
SIM_REVISION from src/game/SimRevision.h in decimal), then each simulation data file
in byte-wise path order; each entry is the path, a 0x00 byte, the content length
as a 64-bit big-endian integer (0xFFFFFFFFFFFFFFFF for a missing file) and the
content with CR LF replaced by LF.

Until every deployed binary reports its own hash (`glob2 --sim-version`), the
engine-agent image passes this value as ENGINE_DATA_HASH; an agent whose binary
reports a different hash refuses to start, so a drifted file list fails loudly.
Keep the static SIM_DATA_FILES plus default manifest discovery identical to
simDataFiles() in the engine. Historical trees without a manifest use the static list.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import sys

SIM_DATA_FILES = (
    'data/maxima/2v2.strategy',
    'data/maxima/base.strategy',
    'data/maxima/duel.strategy',
    'data/maxima/ffa3.strategy',
    'data/maxima/ffa4.strategy',
    'data/maxima/ffa5plus.strategy',
    'data/nicowar.default.txt',
    'data/nicowar.txt',
    'data/usl/Glob2/Runtime/Game.usl',
    'data/usl/Language/Runtime/Classes.usl',
    'data/usl/Language/Runtime/Control.usl',
    'data/usl/Language/Runtime/If.usl',
)


def entry(digest, name, content):
    digest.update(name.encode())
    digest.update(b'\0')
    if content is None:
        digest.update(b'\xff' * 8)
        return
    content = content.replace(b'\r\n', b'\n')
    digest.update(len(content).to_bytes(8, 'big'))
    digest.update(content)


def sim_revision(source):
    """SIM_REVISION, or None for a tree from before it existed (its key hashes no revision)."""
    path = Path(source) / 'src/game/SimRevision.h'
    if not path.is_file():
        return None
    text = path.read_text()
    found = re.search(r'^#define SIM_REVISION (\d+)', text, re.M)
    if not found:
        raise ValueError('src/game/SimRevision.h lacks SIM_REVISION')
    return int(found.group(1))


def sim_data_files(root):
    """Default catalog dependencies are discovered, never a manually maintained list."""
    manifest_path = Path(root) / 'data/buildings/manifest.json'
    if not manifest_path.exists():
        # Historical revisions predate repository catalogs.
        return SIM_DATA_FILES
    manifest = json.loads(manifest_path.read_text())
    names = manifest.get('files')
    if not isinstance(names, list):
        raise ValueError('default building catalog lacks files array')
    seen = set()
    for name in names:
        if (not isinstance(name, str) or not name or name in ('.', '..') or
                any(c in name for c in '/\\:') or name in seen):
            raise ValueError('invalid default building catalog filename')
        seen.add(name)
    resources = ('data/resources/registry.json',) if (Path(root) / 'data/resources/registry.json').exists() else ()
    return tuple(sorted((*SIM_DATA_FILES, *resources, 'data/buildings/manifest.json',
                         *(f'data/buildings/{name}' for name in names))))


def data_hash(root, files=None, revision='read'):
    """revision: SIM_REVISION ('read': from root's src/game/SimRevision.h; None: none)."""
    digest = hashlib.sha256()
    if files is None:
        files = sim_data_files(root)
    if revision == 'read':
        revision = sim_revision(root)
    if revision is not None:
        entry(digest, '#sim-revision', str(revision).encode())
    for name in files:
        path = Path(root) / name
        entry(digest, name, path.read_bytes() if path.is_file() else None)
    return digest.hexdigest()


def version_numbers(root):
    text = (Path(root) / 'src/app/Version.h').read_text()
    minor = re.search(r'^#define VERSION_MINOR (\d+)', text, re.M)
    net = re.search(r'^#define NET_PROTOCOL_VERSION (\d+)', text, re.M)
    if not minor or not net:
        raise ValueError('src/app/Version.h lacks VERSION_MINOR or NET_PROTOCOL_VERSION')
    return int(minor.group(1)), int(net.group(1))


def sim_version_key(source, data=None):
    minor, net = version_numbers(source)
    return f'{minor}-{net}-{data_hash(data or source, revision=sim_revision(source))}'


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument('source', nargs='?', default=Path(__file__).resolve().parents[1],
                        help='source tree holding src/app/Version.h (default: this repository)')
    parser.add_argument('--data-root', help='directory holding data/ (default: the source tree)')
    parser.add_argument('--hash-only', action='store_true', help='print only the data hash')
    parser.add_argument('--expect', help='fail unless the key equals this value (empty: no check)')
    arguments = parser.parse_args()
    root = arguments.data_root or arguments.source
    for name in sim_data_files(root):
        if not (Path(root) / name).is_file():
            print(f'warning: missing simulation data file {name}', file=sys.stderr)
    key = sim_version_key(arguments.source, root)
    if arguments.expect and arguments.expect != key:
        print(f'sim version is {key}, expected {arguments.expect}', file=sys.stderr)
        return 1
    print(key.rsplit('-', 1)[1] if arguments.hash_only else key)
    return 0


if __name__ == '__main__':
    sys.exit(main())
