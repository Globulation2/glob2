#!/usr/bin/env python3
"""Prebuilt master baselines: compare against master without a second checkout.

    python3 tools/dev_baseline.py build [--sha REV] [--variants dev,perf]
    python3 tools/dev_baseline.py path [--sha REV | --merge-base REF] [--variant perf]
    python3 tools/dev_baseline.py list
    python3 tools/dev_baseline.py gc [--keep 3]
    python3 tools/dev_baseline.py seed WORKTREE [--sha REV]

Each baseline is a detached worktree of one master revision under the development
store (`<dev home>/baselines/<host>/<sha>`) with its own builds, so its binaries
run in place and find their runtime assets. `build` makes the dev tier (engine
and unit tests and the game) and the perf tier, and the perf-confirm tier
(`perf=1 opt=3`) with `--variants perf-o3`; the shared compiler cache makes a
baseline one commit newer than the last mostly cache hits. Run it after merges,
from a scheduled job or by hand: `build` with no revision builds origin/master.

`path` prints a baseline's build directory, for benchmark tools and
tools/perf_ab.py --baseline master; `--merge-base HEAD` picks the baseline of the
current branch's merge base. Consumers hold a shared lease, so `gc`, which keeps
the newest --keep baselines, never removes one in use.

`seed` copies a baseline's dev build directory into a fresh worktree of a nearby
revision (copy-on-write where the filesystem supports it) so that its first build
recompiles only what differs; it reports what SCons would rebuild.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
import platform
import shutil
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'scons'))
import dev_store  # noqa: E402
from build_layout import build_identity, default_directory  # noqa: E402

VARIANTS = {
    'dev': {'arguments': {'dev_fast': '1', 'linker': 'auto'}, 'command': []},
    'perf': {'arguments': {'perf': '1'}, 'command': ['perf=1']},
    # perf-confirm tier: quoted performance numbers and tools/inline_guard.py.
    'perf-o3': {'arguments': {'perf': '1', 'opt': '3'}, 'command': ['perf=1', 'opt=3']},
}
TARGETS = ['engine-tests', 'unit-tests']


def git(*args: str, cwd: Path = ROOT) -> str:
    return subprocess.check_output(['git', '-C', str(cwd), *args], text=True).strip()


def host() -> str:
    compiler = os.environ.get('CXX') or shutil.which('c++') or 'c++'
    version = subprocess.run([compiler, '--version'], capture_output=True, text=True).stdout
    digest = hashlib.sha256(version.encode()).hexdigest()[:12]
    return f'{platform.system().lower()}-{platform.machine()}-{digest}'


def home() -> Path:
    path = dev_store.home() / 'baselines' / host()
    path.mkdir(parents=True, exist_ok=True)
    return path


def resolve(sha: str | None, merge_base: str | None) -> str:
    if merge_base:
        return git('merge-base', merge_base, 'origin/master')
    return git('rev-parse', '--verify', (sha or 'origin/master') + '^{commit}')


def build_directory(worktree: Path, variant: str) -> Path:
    return worktree / default_directory(build_identity(VARIANTS[variant]['arguments']))


def build(sha: str, variants: list[str], jobs: int) -> Path:
    worktree = home() / sha
    if (worktree / '.baseline-complete').exists():
        return worktree
    with dev_store.Lease(worktree, exclusive=True):
        if not worktree.exists():
            subprocess.run(['git', '-C', str(ROOT), 'worktree', 'add', '--detach', str(worktree), sha], check=True)
        record = {'sha': sha, 'host': host(), 'started': time.time(), 'variants': {}}
        for variant in variants:
            directory = build_directory(worktree, variant)
            command = [sys.executable, 'tools/dev_build.py', f'-j{jobs}', *VARIANTS[variant]['command'],
                       str((directory / 'src/glob2').relative_to(worktree)), *TARGETS]
            subprocess.run(command, cwd=worktree, check=True)
            record['variants'][variant] = str(directory)
        record['finished'] = time.time()
        (worktree / 'baseline.json').write_text(json.dumps(record, indent=2) + '\n')
        (worktree / '.baseline-complete').touch()
    return worktree


def baselines() -> list[dict]:
    found = []
    for record in home().glob('*/baseline.json'):
        if (record.parent / '.baseline-complete').exists():
            found.append(json.loads(record.read_text()) | {'worktree': str(record.parent)})
    return sorted(found, key=lambda item: item['finished'], reverse=True)


def collect(keep: int) -> list[str]:
    removed = []
    for item in baselines()[keep:]:
        worktree = Path(item['worktree'])
        try:
            lease = dev_store.Lease(worktree, exclusive=True, blocking=False)
        except OSError:
            continue  # in use by a consumer
        with lease:
            (worktree / '.baseline-complete').unlink(missing_ok=True)
            subprocess.run(['git', '-C', str(ROOT), 'worktree', 'remove', '--force', str(worktree)], check=False)
            shutil.rmtree(worktree, ignore_errors=True)
        removed.append(item['sha'])
    return removed


def copy_tree(source: Path, destination: Path) -> None:
    """Copy-on-write where possible (APFS clonefile via cp -c, Linux reflinks)."""
    destination.parent.mkdir(parents=True, exist_ok=True)
    if platform.system() == 'Darwin':
        command = ['cp', '-cR', str(source), str(destination)]
    else:
        command = ['cp', '-a', '--reflink=auto', str(source), str(destination)]
    if subprocess.run(command).returncode != 0:
        shutil.copytree(source, destination, symlinks=True)


def seed(target: Path, sha: str) -> int:
    worktree = build(sha, ['dev'], os.cpu_count() or 2) if not (home() / sha / '.baseline-complete').exists() else home() / sha
    source = build_directory(worktree, 'dev')
    destination = build_directory(target.resolve(), 'dev')
    if destination.exists():
        raise SystemExit(f'{destination} exists; seed only fresh worktrees')
    copy_tree(source, destination)
    for lock in destination.rglob('.build-lock'):
        lock.unlink()
    dry = subprocess.run([sys.executable, 'tools/dev_build.py', '-n', '--debug=explain', *TARGETS],
                         cwd=target, capture_output=True, text=True)
    rebuilt = sum(1 for line in dry.stdout.splitlines() if line.startswith('scons: rebuilding') and ".o'" in line)
    print(f'seeded {destination} from {sha[:12]}; the next build recompiles {rebuilt} object(s)')
    return 0


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    commands = parser.add_subparsers(dest='command', required=True)
    build_parser = commands.add_parser('build')
    build_parser.add_argument('--sha')
    build_parser.add_argument('--variants', default='dev,perf')
    build_parser.add_argument('-j', '--jobs', type=int, default=os.cpu_count() or 2)
    path_parser = commands.add_parser('path')
    path_parser.add_argument('--sha')
    path_parser.add_argument('--merge-base', metavar='REF')
    path_parser.add_argument('--variant', choices=sorted(VARIANTS), default='perf')
    commands.add_parser('list')
    gc_parser = commands.add_parser('gc')
    gc_parser.add_argument('--keep', type=int, default=3)
    seed_parser = commands.add_parser('seed')
    seed_parser.add_argument('worktree', type=Path)
    seed_parser.add_argument('--sha')
    args = parser.parse_args(argv)

    if args.command == 'build':
        print(build(resolve(args.sha, None), args.variants.split(','), args.jobs))
    elif args.command == 'path':
        sha = resolve(args.sha, args.merge_base)
        worktree = home() / sha
        if not (worktree / '.baseline-complete').exists():
            raise SystemExit(f'no baseline for {sha[:12]}; run python3 tools/dev_baseline.py build --sha {sha}')
        print(build_directory(worktree, args.variant))
    elif args.command == 'list':
        print(json.dumps(baselines(), indent=2))
    elif args.command == 'gc':
        print(json.dumps({'removed': collect(args.keep)}))
    else:
        return seed(args.worktree, resolve(args.sha, None))
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
