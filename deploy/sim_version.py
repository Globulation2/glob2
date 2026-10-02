#!/usr/bin/env python3
"""Print the sim version key (<VERSION_MINOR>-<NET_PROTOCOL_VERSION>-<data hash>) of a source tree.

The engine-agent image is labelled with this key and its agent serves exactly
this sim version. The data hash follows the engine's simDataHash() in
src/online/SimVersion.cpp: SHA-256 over, for each simulation data file in
byte-wise path order, the path, a 0x00 byte, the content length as a 64-bit
big-endian integer (0xFFFFFFFFFFFFFFFF for a missing file) and the content with
CR LF replaced by LF.

Until every deployed binary reports its own hash (`glob2 --sim-version`), the
engine-agent image passes this value as ENGINE_DATA_HASH; an agent whose binary
reports a different hash refuses to start, so a drifted file list fails loudly.
Keep SIM_DATA_FILES identical to simDataFiles() in the engine.
"""
import argparse
import hashlib
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


def data_hash(root, files=SIM_DATA_FILES):
    digest = hashlib.sha256()
    for name in files:
        path = Path(root) / name
        digest.update(name.encode())
        digest.update(b'\0')
        if path.is_file():
            content = path.read_bytes().replace(b'\r\n', b'\n')
            digest.update(len(content).to_bytes(8, 'big'))
            digest.update(content)
        else:
            digest.update(b'\xff' * 8)
    return digest.hexdigest()


def version_numbers(root):
    text = (Path(root) / 'src/Version.h').read_text()
    minor = re.search(r'^#define VERSION_MINOR (\d+)', text, re.M)
    net = re.search(r'^#define NET_PROTOCOL_VERSION (\d+)', text, re.M)
    if not minor or not net:
        raise ValueError('src/Version.h lacks VERSION_MINOR or NET_PROTOCOL_VERSION')
    return int(minor.group(1)), int(net.group(1))


def sim_version_key(source, data=None):
    minor, net = version_numbers(source)
    return f'{minor}-{net}-{data_hash(data or source)}'


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument('source', nargs='?', default=Path(__file__).resolve().parents[1],
                        help='source tree holding src/Version.h (default: this repository)')
    parser.add_argument('--data-root', help='directory holding data/ (default: the source tree)')
    parser.add_argument('--hash-only', action='store_true', help='print only the data hash')
    parser.add_argument('--expect', help='fail unless the key equals this value (empty: no check)')
    arguments = parser.parse_args()
    root = arguments.data_root or arguments.source
    for name in SIM_DATA_FILES:
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
