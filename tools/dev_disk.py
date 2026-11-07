"""Disk reports and reclamation for development checkouts (tools/dev_environment.py).

    dev_environment.py worktrees            every worktree: size, activity, branch, PR state
    dev_environment.py worktrees --remove PATH [--discard] [--yes]
    dev_environment.py builds [--gc --idle 14]
    dev_environment.py disk                 where the space goes, largest first

Nothing is removed without --remove/--gc. Removal refuses locked or busy trees,
and dirty trees unless --discard, and asks for confirmation unless --yes. Pull
request state comes from `gh` when it is available, which also identifies
squash-merged branches that `git branch --merged` cannot see.
"""
from __future__ import annotations

import concurrent.futures
import json
import os
import shutil
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
DAY = 86400


def git(path: Path, *args: str, check: bool = False) -> str:
    result = subprocess.run(['git', '-C', str(path), *args], capture_output=True, text=True, check=check)
    return result.stdout.strip() if result.returncode == 0 else ''


def size(path: Path) -> int:
    """Allocated bytes under a path, not following symlinks (parallel-friendly)."""
    total = 0
    stack = [path]
    while stack:
        try:
            with os.scandir(stack.pop()) as entries:
                for entry in entries:
                    try:
                        if entry.is_dir(follow_symlinks=False):
                            stack.append(Path(entry.path))
                        elif entry.is_file(follow_symlinks=False):
                            stat = entry.stat(follow_symlinks=False)
                            total += getattr(stat, 'st_blocks', 0) * 512 or stat.st_size
                    except OSError:
                        pass
        except OSError:
            pass
    return total


def sizes(paths: list[Path]) -> dict[Path, int]:
    with concurrent.futures.ThreadPoolExecutor(max_workers=min(16, len(paths) or 1)) as pool:
        return dict(zip(paths, pool.map(size, paths)))


def gib(value: int) -> str:
    return f'{value / 2**30:7.2f} GiB'


def worktree_list(repository: Path = ROOT) -> list[dict]:
    entries, current = [], {}
    for line in git(repository, 'worktree', 'list', '--porcelain').splitlines() + ['']:
        if not line:
            if current:
                entries.append(current)
            current = {}
        elif line.startswith('worktree '):
            current['path'] = line[9:]
        elif line.startswith('branch '):
            current['branch'] = line[7:].removeprefix('refs/heads/')
        elif line.startswith('HEAD '):
            current['head'] = line[5:]
        elif line.startswith('locked'):
            current['locked'] = True
        elif line == 'detached':
            current['detached'] = True
    return entries


def pull_requests(repository: Path = ROOT) -> dict[str, dict]:
    """Pull requests by head branch, newest first; empty without gh."""
    if not shutil.which('gh'):
        return {}
    result = subprocess.run(['gh', 'pr', 'list', '--state', 'all', '--limit', '1000', '--json',
                             'headRefName,state,mergedAt,number'], cwd=repository, capture_output=True, text=True)
    if result.returncode != 0:
        return {}
    found: dict[str, dict] = {}
    for pr in json.loads(result.stdout):
        found.setdefault(pr['headRefName'], pr)
    return found


def last_activity(path: Path) -> float:
    candidates = [path / '.git', path / 'build']
    stamps = []
    for candidate in candidates:
        try:
            stamps.append(candidate.stat().st_mtime)
        except OSError:
            pass
    for marker in (path / 'build').glob('**/.last-use') if (path / 'build').is_dir() else ():
        try:
            stamps.append(marker.stat().st_mtime)
        except OSError:
            pass
    head = git(path, 'log', '-1', '--format=%ct')
    if head.isdigit():
        stamps.append(float(head))
    return max(stamps, default=0.0)


def busy(path: Path) -> bool:
    """A process has its working directory inside the tree, or a build holds a lock."""
    try:
        sys.path.insert(0, str(ROOT / 'tools'))
        import dev_environment
        if dev_environment.busy(path):
            return True
    except Exception:  # noqa: BLE001 - conservative: unknown means busy
        return True
    for lock in (path / 'build').glob('**/.build-lock') if (path / 'build').is_dir() else ():
        if locked(lock):
            return True
    return False


def locked(lock: Path) -> bool:
    if os.name == 'nt':
        return True
    import fcntl
    try:
        with lock.open('a+b') as handle:
            fcntl.flock(handle.fileno(), fcntl.LOCK_EX | fcntl.LOCK_NB)
    except OSError:
        return True
    return False


def worktrees(repository: Path = ROOT, measure: bool = True) -> list[dict]:
    entries = worktree_list(repository)
    prs = pull_requests(repository)
    master = git(repository, 'rev-parse', '--verify', 'origin/master') or git(repository, 'rev-parse', 'master')
    paths = [Path(entry['path']) for entry in entries]
    measured = sizes([p / name for p in paths for name in ('build', 'artifacts')]) if measure else {}
    rows = []
    for entry, path in zip(entries, paths):
        branch = entry.get('branch')
        pr = prs.get(branch) if branch else None
        merged = bool(master and entry.get('head') and
                      subprocess.run(['git', '-C', str(repository), 'merge-base', '--is-ancestor', entry['head'], master],
                                     capture_output=True).returncode == 0)
        dirty = bool(git(path, 'status', '--porcelain', '--untracked-files=no')) if path.is_dir() else False
        rows.append({
            'path': str(path), 'branch': branch, 'detached': entry.get('detached', False),
            'locked': entry.get('locked', False), 'dirty': dirty,
            'merged_into_master': merged,
            'pr': {'number': pr['number'], 'state': pr['state'], 'merged_at': pr['mergedAt']} if pr else None,
            'scratch': str(path).startswith(('/tmp', '/private/tmp')) or '/.claude/worktrees/' in str(path),
            'idle_days': round((time.time() - last_activity(path)) / DAY, 1) if path.is_dir() else None,
            'build_bytes': measured.get(path / 'build', 0), 'artifacts_bytes': measured.get(path / 'artifacts', 0),
        })
    return rows


def finished(row: dict) -> bool:
    """Merged (including squash merges, via the PR) or closed, and nothing local left to lose."""
    pr_done = row['pr'] is not None and row['pr']['state'] in ('MERGED', 'CLOSED')
    return (row['merged_into_master'] or pr_done) and not row['dirty'] and not row['locked']


def remove_worktree(path: Path, discard: bool, confirm, repository: Path = ROOT) -> str:
    rows = {row['path']: row for row in worktrees(repository, measure=False)}
    row = rows.get(str(path.resolve()))
    if row is None:
        return f'{path}: not a worktree of {repository}'
    if Path(row['path']).resolve() == repository.resolve():
        return f'{path}: refusing to remove the main checkout'
    if row['locked']:
        return f'{path}: locked (git worktree unlock first)'
    if row['dirty'] and not discard:
        return f'{path}: has uncommitted changes; pass --discard to drop them'
    if busy(path):
        return f'{path}: busy (a process or build is using it)'
    if not confirm(f'Remove worktree {path} (branch {row["branch"]})? [y/N] '):
        return f'{path}: kept'
    command = ['git', '-C', str(repository), 'worktree', 'remove'] + (['--force'] if discard else []) + [str(path)]
    result = subprocess.run(command, capture_output=True, text=True)
    return f'{path}: removed' if result.returncode == 0 else f'{path}: {result.stderr.strip()}'


def builds(checkouts: list[Path]) -> list[dict]:
    rows = []
    for checkout in checkouts:
        for identity in (checkout / 'build').glob('**/identity.json') if (checkout / 'build').is_dir() else ():
            directory = identity.parent
            if any(parent.name == 'compilers' for parent in directory.parents):
                continue
            stamp = directory / '.last-use'
            used = stamp.stat().st_mtime if stamp.exists() else identity.stat().st_mtime
            rows.append({'path': str(directory), 'checkout': str(checkout), 'idle_days': round((time.time() - used) / DAY, 1),
                         'busy': (directory / '.build-lock').exists() and locked(directory / '.build-lock')})
    measured = sizes([Path(row['path']) for row in rows])
    for row in rows:
        row['bytes'] = measured[Path(row['path'])]
    return sorted(rows, key=lambda row: -row['bytes'])


def gc_builds(rows: list[dict], idle_days: float, apply: bool) -> list[dict]:
    removed = []
    for row in rows:
        if row['idle_days'] >= idle_days and not row['busy']:
            if apply:
                shutil.rmtree(row['path'], ignore_errors=True)
            removed.append(row)
    return removed


def disk(checkouts: list[Path], store_home: Path) -> list[dict]:
    """Largest consumers first: per-checkout build/artifacts/source, store, git objects."""
    targets: dict[str, Path] = {}
    for checkout in checkouts:
        targets[f'build {checkout}'] = checkout / 'build'
        targets[f'artifacts {checkout}'] = checkout / 'artifacts'
    common = git(ROOT, 'rev-parse', '--path-format=absolute', '--git-common-dir')
    if common:
        targets['git objects'] = Path(common) / 'objects'
    if store_home.is_dir():
        for child in sorted(store_home.iterdir()):
            targets[f'store {child.name}'] = child
    measured = sizes(list(targets.values()))
    rows = [{'what': name, 'path': str(path), 'bytes': measured[path]} for name, path in targets.items()]
    return sorted((row for row in rows if row['bytes']), key=lambda row: -row['bytes'])


def stale_toolchains(checkouts: list[Path], home: Path) -> list[Path]:
    """Toolchain installations no registered checkout would select.

    Mobile and Emscripten toolchains are keyed by their checkout manifests
    (dev_store.mobile_tools, dev_store.browser_sdk); a key no checkout produces is
    stale. Shared archive components are stale when no kept toolchain links to them."""
    sys.path.insert(0, str(ROOT / 'scons'))
    import dev_store
    manifests = {
        'mobile': ['platforms/mobile/toolchain.json', 'platforms/mobile/android-tools.json', 'platforms/mobile/emulator.json'],
        'emscripten': ['platforms/web/toolchain.json'],
    }
    wanted: dict[str, set[str]] = {kind: set() for kind in manifests}
    for checkout in checkouts:
        for kind, files in manifests.items():
            if all((checkout / name).is_file() for name in files):
                wanted[kind].add(dev_store.key(checkout, files))
    stale, kept = [], []
    for kind in manifests:
        for entry in sorted((home / 'toolchains' / kind).glob('*')) if (home / 'toolchains' / kind).is_dir() else ():
            (kept if entry.name in wanted[kind] else stale).append(entry)
    linked = set()
    for tree in kept:
        for directory, names, files in os.walk(tree):
            for name in names + files:
                path = Path(directory) / name
                if path.is_symlink():
                    try:
                        linked.add(path.resolve())
                    except OSError:
                        pass
            names[:] = [n for n in names if not (Path(directory) / n).is_symlink()]
    artifacts = home / 'toolchains' / 'artifacts'
    for component in sorted(artifacts.glob('*')) if artifacts.is_dir() else ():
        resolved = component.resolve()
        if not any(target == resolved or resolved in target.parents for target in linked):
            stale.append(component)
    return stale


def prune_toolchains(checkouts: list[Path], home: Path, apply: bool) -> list[dict]:
    sys.path.insert(0, str(ROOT / 'scons'))
    import dev_store
    report = []
    for path in stale_toolchains(checkouts, home):
        entry = {'path': str(path), 'bytes': size(path), 'removed': False}
        if apply:
            try:
                with dev_store.Lease(path, exclusive=True, blocking=False, track_use=False):
                    shutil.rmtree(path)
                    entry['removed'] = True
            except ValueError:
                entry['busy'] = True
        report.append(entry)
    return report
