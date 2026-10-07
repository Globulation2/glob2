#!/usr/bin/env python3
"""Check that the sim version key follows the simulation (src/game/SimRevision.h).

Two checks, both cheap and build-free:

1. The committed match record test/fixtures/multiplayer/FourSquares1.g2mr names
   this tree's sim version key (deploy/sim_version.py). Bumping SIM_REVISION,
   VERSION_MINOR, NET_PROTOCOL_VERSION or a simulation data file therefore
   requires regenerating the record (and its trace) in the same change.
2. With --base, when the committed verification trace or record differs from
   the base revision, the sim version key must differ too: a change that moves
   the golden trace changed the simulation and must bump SIM_REVISION.

The cross-platform CI job separately fails when every platform agrees on a
trace that differs from the committed one. Together: a simulation change that
reaches the golden match cannot land without a new sim version.

Exit status 0 when both hold, 1 otherwise (with the fix in the message).
"""
import argparse
import importlib.util
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
RECORD = 'test/fixtures/multiplayer/FourSquares1.g2mr'
TRACE = 'test/fixtures/multiplayer/FourSquares1.verify-trace.txt'
# What the sim version key is computed from (deploy/sim_version.py).
KEY_INPUTS = ('src/app/Version.h', 'src/game/SimRevision.h')
FIX = ('bump SIM_REVISION in src/game/SimRevision.h, then regenerate the record and trace with '
       "python3 test/run_tests.py --update-fixtures --filter 'TurnEngineHarness/the committed*'")


def load_sim_version():
    spec = importlib.util.spec_from_file_location('sim_version', ROOT / 'deploy/sim_version.py')
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def record_sim_version(data):
    """The simVersion text of a G2MR record (docs/multiplayer/turn-protocol.md)."""
    if data[:4] != b'G2MR':
        raise ValueError('not a G2MR match record')
    offset = 10  # magic, formatVersion u16, flags u32
    for _ in range(2):  # matchId, then simVersion (text32 each)
        length = int.from_bytes(data[offset:offset + 4], 'big')
        text = data[offset + 4:offset + 4 + length]
        offset += 4 + length
    return text.decode()


def git(root, *args, check=True):
    return subprocess.run(['git', '-C', str(root), *args], check=check, capture_output=True)


def file_at(root, revision, path):
    """A file's bytes at a revision, or None when it does not exist there."""
    shown = git(root, 'show', f'{revision}:{path}', check=False)
    return shown.stdout if shown.returncode == 0 else None


def key_at(root, revision, sim_version):
    """The sim version key of a revision of the tree, from its own files."""
    with tempfile.TemporaryDirectory() as scratch:
        tree = Path(scratch)
        catalog_files = git(root, 'ls-tree', '-r', '--name-only', revision, 'data/buildings', 'data/resources', check=False)
        dependencies = tuple(catalog_files.stdout.decode().splitlines()) if catalog_files.returncode == 0 else ()
        for path in KEY_INPUTS + sim_version.SIM_DATA_FILES + dependencies:
            content = file_at(root, revision, path)
            if content is not None:
                (tree / path).parent.mkdir(parents=True, exist_ok=True)
                (tree / path).write_bytes(content)
        # A tree from before SIM_REVISION gets its own (revision-less) key.
        return sim_version.sim_version_key(tree)


def check(root, base=None):
    """Problems found, as messages; empty when everything holds."""
    sim_version = load_sim_version()
    problems = []
    key = sim_version.sim_version_key(root)
    recorded = record_sim_version((root / RECORD).read_bytes())
    if recorded != key:
        problems.append(f'{RECORD} was recorded with sim version {recorded}, but this tree is {key}; '
                        "regenerate it with python3 test/run_tests.py --update-fixtures "
                        "--filter 'TurnEngineHarness/the committed*'")
    if base:
        if git(root, 'cat-file', '-e', f'{base}^{{commit}}', check=False).returncode != 0:
            git(root, 'fetch', '--no-tags', '--depth=1', 'origin', base, check=False)
        if git(root, 'cat-file', '-e', f'{base}^{{commit}}', check=False).returncode != 0:
            problems.append(f'cannot read the base revision {base}')
            return problems
        moved = [path for path in (TRACE, RECORD)
                 if file_at(root, base, path) is not None and file_at(root, base, path) != (root / path).read_bytes()]
        if moved:
            base_key = key_at(root, base, sim_version)
            if base_key == key:
                problems.append(f'{" and ".join(moved)} changed, so the simulation changed, but the sim version '
                                f'is still {key}; {FIX}')
        base_revision = file_at(root, base, 'src/game/SimRevision.h')
        if base_revision is not None:
            with tempfile.TemporaryDirectory() as scratch:
                (Path(scratch) / 'src/game').mkdir(parents=True)
                (Path(scratch) / 'src/game/SimRevision.h').write_bytes(base_revision)
                before = sim_version.sim_revision(scratch)
            if sim_version.sim_revision(root) < before:
                problems.append(f'SIM_REVISION went down from {before}; it only ever increases')
    return problems


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument('--base', help='base revision to compare with (pull request base, previous push)')
    parser.add_argument('--root', type=Path, default=ROOT, help='source tree (default: this repository)')
    arguments = parser.parse_args()
    base = arguments.base if arguments.base and set(arguments.base) != {'0'} else None
    problems = check(arguments.root.resolve(), base)
    for problem in problems:
        print(f'::error::{problem}')
    if not problems:
        print('The sim version key matches the committed match record'
              + (' and follows every change of the golden trace' if base else ''))
    return 1 if problems else 0


if __name__ == '__main__':
    sys.exit(main())
