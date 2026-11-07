#!/usr/bin/env python3
"""Regenerate every fixture keyed to the simulation version, in one command.

Run it after bumping SIM_REVISION (src/game/SimRevision.h) or any change that moves
the golden match, in the same change:

    python3 tools/refresh_sim_fixtures.py
    python3 tools/refresh_sim_fixtures.py --from-run 123456789   # add other platforms' generator rows

Steps, stopping at the first failure:

1. Build the dev tier (tools/dev_build.py): the game, engine tests and the map
   generator golden test. Same-platform checksums match between dev, perf and
   release builds, so the dev build is a valid fixture producer.
2. Rewrite the committed match record and verification trace
   (TurnEngineHarness), then every other [golden] doctest fixture.
3. Regenerate this platform's map generator golden rows; rows for other
   platforms are printed as pending unless --from-run supplies them.
4. Regenerate the telemetry simulation traces and the Maxima save-continuation
   baseline (serial runs).
5. Check the result with test/check_sim_revision.py --base <base>; this fails
   while the match record still names the previous sim version.

--from-run only merges: the `golden-rows-<platform>` artifacts that CI uploads
(MapGeneratorGoldenTest --print on Windows and macOS) replace those platforms'
rows in test/map-generator-golden.txt.
"""
from __future__ import annotations

import argparse
import os
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
GOLDEN_ROWS = ROOT / 'test/map-generator-golden.txt'
EXECUTABLE = '.exe' if os.name == 'nt' else ''


def dev_build_directory() -> Path:
    sys.path.insert(0, str(ROOT / 'scons'))
    try:
        from build_layout import build_identity, default_directory
    finally:
        sys.path.pop(0)
    return ROOT / default_directory(build_identity({'dev_fast': '1', 'linker': 'auto'}))


def run(step: str, command: list[str], **kwargs) -> None:
    print(f'\n== {step}\n$ {" ".join(str(c) for c in command)}', flush=True)
    result = subprocess.run([str(c) for c in command], cwd=ROOT, **kwargs)
    if result.returncode != 0:
        raise SystemExit(f'refresh_sim_fixtures: "{step}" failed with exit status {result.returncode}')


def platform_of(line: str) -> str | None:
    if not line.strip() or line.startswith('#'):
        return None
    return line.split()[0]


def merge_rows(table: str, platform: str, rows: str) -> str:
    """Replace one platform's rows in the golden table, keeping comments and order."""
    kept = [line for line in table.splitlines() if platform_of(line) != platform]
    new = [line for line in rows.splitlines() if platform_of(line) == platform]
    if not new:
        raise ValueError(f'no {platform} rows in the downloaded artifact')
    header = [line for line in kept if platform_of(line) is None]
    body = [line for line in kept if platform_of(line) is not None] + new
    body.sort(key=lambda line: line.split()[0])
    return '\n'.join(header + body) + '\n'


def import_run_rows(run_id: str) -> list[str]:
    """Merge every golden-rows-* artifact of a CI run; the rows name their own platform."""
    found: dict[str, str] = {}
    with tempfile.TemporaryDirectory(prefix='glob2-golden-rows-') as directory:
        run('download generator rows', ['gh', 'run', 'download', run_id, '--pattern', 'golden-rows-*', '--dir', directory])
        for path in sorted(Path(directory).rglob('*.txt')):
            text = path.read_text()
            for platform in sorted({p for line in text.splitlines() if (p := platform_of(line))}):
                rows = '\n'.join(line for line in text.splitlines() if platform_of(line) == platform)
                if platform in found and found[platform] != rows:
                    raise SystemExit(f'CI artifacts disagree about the {platform} rows; check the run')
                found[platform] = rows
    for platform, rows in found.items():
        GOLDEN_ROWS.write_text(merge_rows(GOLDEN_ROWS.read_text(), platform, rows))
    return sorted(found)


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--build-dir', type=Path, help='use this existing build instead of the dev build')
    parser.add_argument('--no-build', action='store_true', help='skip step 1 (the build must be current)')
    parser.add_argument('--from-run', metavar='RUN_ID', help='only merge golden-rows-<platform> artifacts from a CI run')
    parser.add_argument('--base', default='origin/master', help='revision for the final sim-version check')
    parser.add_argument('-j', '--jobs', type=int, default=os.cpu_count() or 2)
    args = parser.parse_args(argv)

    build = (args.build_dir or dev_build_directory()).resolve()
    if args.from_run:
        print('merged generator rows for: ' + ', '.join(import_run_rows(args.from_run)))
        return 0
    if not args.no_build and not args.build_dir:
        run('build the dev tier', [sys.executable, 'tools/dev_build.py', f'-j{args.jobs}',
                                   str((build / 'src/glob2').relative_to(ROOT)), 'engine-tests', 'map-generator-golden-test'])
    tests = [sys.executable, 'test/run_tests.py', '--build-dir', build, '--binary', 'engine', '-j', str(args.jobs)]
    run('committed match record and trace', tests + ['--update-fixtures', '--filter', 'TurnEngineHarness/the committed*'])
    run('[golden] doctest fixtures', tests + ['--update-fixtures', '--tag', 'golden'])
    with tempfile.TemporaryDirectory(prefix='glob2-golden-profile-') as profile:
        run('map generator rows for this platform', [build / f'src/MapGeneratorGoldenTest{EXECUTABLE}', profile, '--update'])
    glob2 = build / f'src/glob2{EXECUTABLE}'
    run('telemetry simulation traces', [sys.executable, 'test/check_telemetry_simulation.py', glob2, '--update-fixtures'])
    run('Maxima save continuation baseline', [sys.executable, 'test/maxima/check_save_continuation_fixture.py', glob2, '--update-fixtures'])
    run('sim version check', [sys.executable, 'test/check_sim_revision.py', '--base', args.base])
    if not args.from_run:
        print('\nOther platforms keep their existing generator rows. After master CI runs, merge theirs with\n'
              '  python3 tools/refresh_sim_fixtures.py --from-run <run id>')
    print('\nrefresh_sim_fixtures: done; review `git status` and commit the regenerated fixtures with the change.')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
