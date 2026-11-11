#!/usr/bin/env python3
"""Select the test cases a change can affect, from recorded per-case coverage.

A coverage map (written by `test/run_coverage.py --coverage-map`, uploaded by the
master `native-coverage` job as `coverage-map-<sha>.json.gz`) lists, for every
test case, the source files whose functions it executed. A case runs when:

- its recorded files intersect the change,
- it has no record (new or renamed cases), or
- it matches the always-run list (golden simulation, save formats, replays...).

The whole tier runs instead (no selection) when no usable map exists (missing,
older than seven days, or not from an ancestor of the change) or when the change
touches something coverage cannot attribute: a header, the build system, test
infrastructure, content or CI configuration, or a non-C++ file under src/.

    python3 test/select_affected.py --fetch            # newest map from master CI -> artifacts/
    python3 test/select_affected.py --base origin/master --list
    python3 test/run_tests.py --affected               # the headless tier, restricted to the change
"""
from __future__ import annotations

import argparse
import datetime as dt
import fnmatch
import gzip
import json
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
MAX_AGE = dt.timedelta(days=7)
# fnmatch patterns over "Suite/name [tags]" (the binary prefix removed); [[] matches a bracket.
ALWAYS = (
    'TurnEngineHarness/*', '*[[]save-format[]]*', '*[[]golden[]]*', '*Replay*/*',
    'UnitCustomization/*ablation*', 'RuntimeResources/frozen seeded resource compositions*',
    'MapGenerator*Golden*', 'SimVersion*/*',
)
UNATTRIBUTABLE_PREFIXES = ('SConstruct', 'buildsys/', 'src/SConscript', 'libs/gag/src/SConscript', 'libs/usl/src/SConscript',
                           'test/', 'content/', '.github/', 'third_party/', 'vcpkg.json', 'platforms/')
SOURCE_SUFFIXES = ('.cpp', '.cc', '.c', '.mm')


class Selection:
    def __init__(self, cases: set[str] | None, reason: str):
        self.cases = cases  # None: run everything in the tier
        self.reason = reason

    def keeps(self, label: str) -> bool:
        return self.cases is None or label in self.cases


def load_map(path: Path) -> dict:
    data = path.read_bytes()
    if data[:2] == b'\x1f\x8b':
        data = gzip.decompress(data)
    document = json.loads(data)
    if document.get('schema') != 1 or not isinstance(document.get('cases'), dict):
        raise ValueError('coverage map needs schema 1 and a cases object')
    return document


def changed_files(base: str, root: Path = ROOT) -> list[str]:
    merge_base = subprocess.check_output(['git', '-C', str(root), 'merge-base', base, 'HEAD'], text=True).strip()
    committed = subprocess.check_output(['git', '-C', str(root), 'diff', '--name-only', merge_base], text=True)
    untracked = subprocess.check_output(['git', '-C', str(root), 'ls-files', '--others', '--exclude-standard'], text=True)
    return sorted({line for line in (committed + untracked).splitlines() if line})


def unattributable(path: str) -> bool:
    if path.startswith(UNATTRIBUTABLE_PREFIXES):
        return True
    if path.startswith(('src/', 'libs/')):
        return not path.endswith(SOURCE_SUFFIXES)
    return False


def ancestor(sha: str, root: Path = ROOT) -> bool:
    return subprocess.run(['git', '-C', str(root), 'merge-base', '--is-ancestor', sha, 'HEAD'],
                          capture_output=True).returncode == 0


def select(labels: list[str], changes: list[str], coverage: dict | None, now: dt.datetime | None = None,
           is_ancestor=ancestor) -> Selection:
    if coverage is None:
        return Selection(None, 'no coverage map; running the whole tier')
    created = dt.datetime.fromisoformat(coverage.get('created', '1970-01-01T00:00:00+00:00'))
    if (now or dt.datetime.now(dt.timezone.utc)) - created > MAX_AGE:
        return Selection(None, f'coverage map from {created:%Y-%m-%d} is older than seven days')
    if coverage.get('sha') and not is_ancestor(coverage['sha']):
        return Selection(None, f'coverage map {coverage["sha"][:12]} is not an ancestor of HEAD')
    blocking = [path for path in changes if unattributable(path)]
    if blocking:
        return Selection(None, f'{blocking[0]} is not attributable by coverage' +
                         (f' (and {len(blocking) - 1} more)' if len(blocking) > 1 else ''))
    changed = set(changes)
    recorded = coverage['cases']
    chosen = set()
    for label in labels:
        files = recorded.get(label)
        case = label.split(':', 1)[-1]
        if files is None or changed.intersection(files) or any(fnmatch.fnmatchcase(case, p) for p in ALWAYS):
            chosen.add(label)
    return Selection(chosen, f'{len(chosen)} of {len(labels)} cases touch the {len(changed)} changed file(s)')


def fetch(output: Path, repository: Path = ROOT) -> str | None:
    """Download the newest coverage map that master CI uploaded; returns its run id."""
    import tempfile
    runs = json.loads(subprocess.check_output(
        ['gh', 'run', 'list', '--workflow', 'build.yml', '--branch', 'master', '--limit', '30',
         '--json', 'databaseId,status'], cwd=repository, text=True))
    for run in runs:
        if run['status'] != 'completed':
            continue
        with tempfile.TemporaryDirectory() as directory:
            result = subprocess.run(['gh', 'run', 'download', str(run['databaseId']), '--name', 'coverage-map',
                                     '--dir', directory], cwd=repository, capture_output=True)
            found = list(Path(directory).rglob('coverage-map.json.gz'))
            if result.returncode == 0 and found:
                output.parent.mkdir(parents=True, exist_ok=True)
                output.write_bytes(found[0].read_bytes())
                return str(run['databaseId'])
    return None


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--map', type=Path, default=ROOT / 'artifacts' / 'coverage-map.json.gz')
    parser.add_argument('--fetch', action='store_true', help='download the newest map from master CI into --map first')
    parser.add_argument('--base', default='origin/master')
    parser.add_argument('--labels', type=Path, help='file with one case label per line (default: the map\'s cases)')
    parser.add_argument('--list', action='store_true')
    args = parser.parse_args(argv)
    if args.fetch:
        run = fetch(args.map)
        print(f'fetched the coverage map of run {run}' if run else 'no coverage map found in recent master runs')
    coverage = load_map(args.map) if args.map.exists() else None
    labels = args.labels.read_text().splitlines() if args.labels else sorted((coverage or {}).get('cases', {}))
    selection = select(labels, changed_files(args.base), coverage)
    print(selection.reason)
    if args.list and selection.cases is not None:
        print('\n'.join(sorted(selection.cases)))
    return 0


if __name__ == '__main__':
    sys.exit(main())
