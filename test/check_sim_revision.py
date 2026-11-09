#!/usr/bin/env python3
"""Check SIM_REVISION (src/game/SimRevision.h) and report sim version keys.

Cheap and build-free:

1. SIM_REVISION is a plain integer.
2. With --base, it never goes down.

Whether a change needed a bump is judged by sim-diff (.github/workflows/sim-diff.yml),
which compares what the merge base and the pull request simulate and fails when
output differs while the sim version key stayed the same. --key-at REV prints a
revision's key, computed from that revision's own files and deploy/sim_version.py,
for that comparison.

Exit status 0 when the checks hold, 1 otherwise (with the problem in the message).
"""
import argparse
import importlib.util
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
# What the sim version key is computed from (deploy/sim_version.py).
KEY_INPUTS = ('src/app/Version.h', 'src/game/SimRevision.h')


def load_sim_version():
    spec = importlib.util.spec_from_file_location('sim_version', ROOT / 'deploy/sim_version.py')
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def load_sim_version_at(root, revision, current):
    """deploy/sim_version.py as it was at a revision, so a change to what the key
    hashes moves the key; the current module when the revision has none."""
    source = file_at(root, revision, 'deploy/sim_version.py')
    if source is None:
        return current
    with tempfile.TemporaryDirectory() as scratch:
        path = Path(scratch) / 'sim_version.py'
        path.write_bytes(source)
        spec = importlib.util.spec_from_file_location('sim_version_base', path)
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
    return module


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
        # Revisions from before the content/ move hold data/ at the root. The
        # scratch tree always uses the engine's virtual layout.
        prefix = 'content/' if git(root, 'ls-tree', '--name-only', revision, 'content/data', check=False).stdout else ''
        catalog_files = git(root, 'ls-tree', '-r', '--name-only', revision,
                            *(prefix + d for d in ('data/buildings', 'data/resources', 'data/units')), check=False)
        dependencies = tuple(path.removeprefix(prefix) for path in catalog_files.stdout.decode().splitlines()) \
            if catalog_files.returncode == 0 else ()
        for path, stored in [(path, path) for path in KEY_INPUTS] + \
                [(path, prefix + path) for path in sim_version.SIM_DATA_FILES + dependencies]:
            content = file_at(root, revision, stored)
            if content is not None:
                (tree / path).parent.mkdir(parents=True, exist_ok=True)
                (tree / path).write_bytes(content)
        # A tree from before SIM_REVISION gets its own (revision-less) key.
        return sim_version.sim_version_key(tree)


def ensure_commit(root, revision):
    if git(root, 'cat-file', '-e', f'{revision}^{{commit}}', check=False).returncode != 0:
        git(root, 'fetch', '--no-tags', '--depth=1', 'origin', revision, check=False)
    return git(root, 'cat-file', '-e', f'{revision}^{{commit}}', check=False).returncode == 0


def check(root, base=None):
    """Problems found, as messages; empty when everything holds."""
    sim_version = load_sim_version()
    problems = []
    try:
        revision = sim_version.sim_revision(root)
    except Exception as error:  # a malformed define
        return [f'cannot read SIM_REVISION from src/game/SimRevision.h: {error}']
    if revision is None:
        return ['src/game/SimRevision.h is missing']
    if revision < 0:
        problems.append(f'SIM_REVISION must be a non-negative integer, not {revision!r}')
    if base:
        if not ensure_commit(root, base):
            return problems + [f'cannot read the base revision {base}']
        base_revision = file_at(root, base, 'src/game/SimRevision.h')
        if base_revision is not None:
            with tempfile.TemporaryDirectory() as scratch:
                (Path(scratch) / 'src/game').mkdir(parents=True)
                (Path(scratch) / 'src/game/SimRevision.h').write_bytes(base_revision)
                before = sim_version.sim_revision(scratch)
            if revision < before:
                problems.append(f'SIM_REVISION went down from {before}; it only ever increases')
    return problems


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[1])
    parser.add_argument('--base', help='base revision to compare with (pull request base, previous push)')
    parser.add_argument('--key-at', metavar='REV', help="print the sim version key of a revision and exit")
    parser.add_argument('--root', type=Path, default=ROOT, help='source tree (default: this repository)')
    arguments = parser.parse_args()
    root = arguments.root.resolve()
    if arguments.key_at:
        if not ensure_commit(root, arguments.key_at):
            print(f'cannot read the revision {arguments.key_at}', file=sys.stderr)
            return 1
        print(key_at(root, arguments.key_at, load_sim_version_at(root, arguments.key_at, load_sim_version())))
        return 0
    base = arguments.base if arguments.base and set(arguments.base) != {'0'} else None
    problems = check(root, base)
    for problem in problems:
        print(f'::error::{problem}')
    if not problems:
        print('SIM_REVISION is an integer' + (' and did not go down' if base else ''))
    return 1 if problems else 0


if __name__ == '__main__':
    sys.exit(main())
