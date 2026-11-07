#!/usr/bin/env python3
"""Catch hot-path code that stopped being inlined after a header refactor.

    python3 tools/inline_guard.py BASELINE_BUILD CANDIDATE_BUILD
    python3 tools/inline_guard.py --baseline master build/linux/client/perf/o3

Header decoupling moves inline bodies and changes what a translation unit can
see. In an optimized build an accessor that was inlined into a simulation loop
leaves no trace in the object; once it stops being visible it becomes an
undefined (called) symbol instead. This tool lists, per object file in the hot
simulation directories, the undefined symbols that name member functions of the
core simulation classes and appear only in the candidate build. Compare two
builds of the same tier, preferably `perf=1 opt=3`; debug builds inline nothing
and say nothing.

`--baseline master` uses the prebuilt baseline of the merge base
(tools/dev_baseline.py) for the candidate's tier. Exit status 1 means new calls
were found; each must be removed or justified in the pull request.
"""
from __future__ import annotations

import argparse
import json
import re
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

# Objects of the translation units whose loops run per cell, unit, building or tick.
HOT_DIRECTORIES = ('src/map/gradient', 'src/map/pathfind', 'src/field', 'src/unit', 'src/building', 'src/team',
                   'src/engine/sim/snapshot', 'src/resource', 'src/map/claims')
HOT_FILES = ('src/game/Game_sync*.o', 'src/map/Map*.o')
# Classes (or namespaces) whose member functions a hot loop must not start calling.
CLASSES = ('Map', 'Game', 'Unit', 'Building', 'Team', 'BaseTeam', 'TerrainRegistry', 'MapRouting', 'MapClaims',
           'MapState', 'SimulationSnapshot', 'BuildingType', 'UnitCatalog', 'ResourceRegistry', 'CellRuleTable')
MEMBER = re.compile(r'^(?:[\w:<>, ]+\s)?(' + '|'.join(CLASSES) + r')::[~\w]+(?:<.*>)?\(')


def hot_objects(build: Path) -> dict[str, Path]:
    found: dict[str, Path] = {}
    for directory in HOT_DIRECTORIES:
        for path in (build / directory).rglob('*.o'):
            found[str(path.relative_to(build))] = path
    for pattern in HOT_FILES:
        for path in build.glob(pattern):
            found[str(path.relative_to(build))] = path
    return found


def undefined_members(path: Path) -> set[str]:
    nm = shutil.which('nm') or 'nm'
    output = subprocess.run([nm, '-C', '--undefined-only', '--format=just-symbols', str(path)],
                            capture_output=True, text=True).stdout
    return {line.strip() for line in output.splitlines() if MEMBER.match(line.strip())}


def compare(baseline: Path, candidate: Path) -> dict:
    before, after = hot_objects(baseline), hot_objects(candidate)
    if not after:
        raise SystemExit(f'no hot objects under {candidate}; pass a build directory such as build/linux/client/perf/o3')
    report = {'baseline': str(baseline), 'candidate': str(candidate), 'objects': len(after), 'new_calls': {},
              'new_objects': sorted(set(after) - set(before))}
    for name in sorted(set(after) & set(before)):
        added = undefined_members(after[name]) - undefined_members(before[name])
        if added:
            report['new_calls'][name] = sorted(added)
    return report


def tier_of(build: Path) -> str:
    try:
        identity = json.loads((build / 'identity.json').read_text())
    except (OSError, ValueError):
        identity = {}
    if identity.get('mode') != 'perf':
        return 'dev'
    return 'perf-o3' if identity.get('opt') == 3 else 'perf'


def master_baseline(candidate: Path) -> Path:
    command = [sys.executable, str(ROOT / 'tools/dev_baseline.py'), 'path', '--merge-base', 'HEAD',
               '--variant', tier_of(candidate)]
    return Path(subprocess.check_output(command, text=True).strip())


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('builds', nargs='+', type=Path, metavar='BUILD', help='[BASELINE] CANDIDATE')
    parser.add_argument('--baseline', help="'master' for the merge base's prebuilt baseline")
    parser.add_argument('--json', action='store_true')
    args = parser.parse_args(argv)
    if args.baseline == 'master' and len(args.builds) == 1:
        baseline, candidate = master_baseline(args.builds[0]), args.builds[0]
    elif len(args.builds) == 2 and not args.baseline:
        baseline, candidate = args.builds
    else:
        parser.error('pass BASELINE CANDIDATE, or --baseline master CANDIDATE')
    report = compare(baseline.resolve(), candidate.resolve())
    if args.json:
        print(json.dumps(report, indent=1))
    else:
        for name, symbols in report['new_calls'].items():
            print(name)
            for symbol in symbols:
                print(f'    calls {symbol}')
        if report['new_objects']:
            print(f"{len(report['new_objects'])} new hot object(s), not compared: {', '.join(report['new_objects'])}")
        print(f"inline guard: {len(report['new_calls'])} of {report['objects']} hot objects gained calls into core classes")
    return 1 if report['new_calls'] else 0


if __name__ == '__main__':
    raise SystemExit(main())
