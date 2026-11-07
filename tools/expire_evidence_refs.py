#!/usr/bin/env python3
"""Expire the evidence refs that keep large blobs alive in the git object store.

Evidence now lives in pull request descriptions (AGENTS.md). Older evidence was
kept on refs that master never reaches — evidence/* branches, codex/snapshots,
codex/turn-diffs, archive/* — and they hold most of the object store. This tool:

    python3 tools/expire_evidence_refs.py                 # report refs and the space only they hold
    python3 tools/expire_evidence_refs.py --apply         # delete them locally, expire reflogs, repack
    python3 tools/expire_evidence_refs.py --apply --remote  # also delete them on origin (irreversible)

--apply asks for confirmation unless --yes. Deleting on origin breaks links that
point at those refs; that is accepted by the decision recorded in
docs/development/development-storage.md, and is the maintainer's call to make.
Worktree HEADs and every branch reachable from them survive the repack.
"""
from __future__ import annotations

import argparse
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
PATTERN = re.compile(r'^refs/(heads/evidence/|remotes/origin/evidence/|codex/snapshots/|codex/turn-diffs/|archive/|tags/(codex|archive)/)')
REMOTE_PATTERN = re.compile(r'^refs/(heads/evidence/|codex/snapshots/|codex/turn-diffs/|archive/|tags/(codex|archive)/)')


def git(*args: str, **kwargs) -> str:
    return subprocess.run(['git', '-C', str(ROOT), *args], check=True, capture_output=True, text=True, **kwargs).stdout


def local_refs() -> list[str]:
    return [ref for ref in git('for-each-ref', '--format=%(refname)').split() if PATTERN.match(ref)]


def remote_refs() -> list[str]:
    return [line.split()[1] for line in git('ls-remote', 'origin').splitlines() if REMOTE_PATTERN.match(line.split()[1])]


def exclusive_bytes(refs: list[str]) -> int:
    """Disk use of objects reachable from refs but not from any other ref or worktree HEAD."""
    keep = [ref for ref in git('for-each-ref', '--format=%(refname)').split() if ref not in set(refs)]
    heads = [line.split()[1] for line in git('worktree', 'list', '--porcelain').splitlines() if line.startswith('HEAD ')]
    if not refs:
        return 0
    arguments = ['rev-list', '--objects', '--disk-usage', *refs, '--not', *keep, *heads]
    return int(git(*arguments).strip() or 0)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--apply', action='store_true')
    parser.add_argument('--remote', action='store_true', help='with --apply, also delete the refs on origin')
    parser.add_argument('--yes', action='store_true')
    args = parser.parse_args()
    refs = local_refs()
    remote = remote_refs() if args.remote or not args.apply else []
    print(f'{len(refs)} local evidence refs; {len(remote)} on origin')
    print(f'objects only they hold: {exclusive_bytes(refs) / 2**30:.2f} GiB')
    if not args.apply:
        print('dry run; pass --apply (and --remote for origin) to expire them')
        return 0
    if not args.yes and input('Delete these refs and repack? [y/N] ').strip().lower() != 'y':
        return 1
    if args.remote and remote:
        for start in range(0, len(remote), 100):
            subprocess.run(['git', '-C', str(ROOT), 'push', 'origin', *[':' + ref for ref in remote[start:start + 100]]], check=True)
    for ref in refs:
        subprocess.run(['git', '-C', str(ROOT), 'update-ref', '-d', ref], check=True)
    git('fetch', '--prune', 'origin')
    git('reflog', 'expire', '--expire=now', '--all')
    subprocess.run(['git', '-C', str(ROOT), 'repack', '-a', '-d', '-f', '--window=250', '--write-bitmap-index'], check=True)
    subprocess.run(['git', '-C', str(ROOT), 'prune'], check=True)
    print('done; see `du -sh .git/objects`')
    return 0


if __name__ == '__main__':
    sys.exit(main())
