#!/usr/bin/env python3
"""Regenerate every fixture keyed to the simulation version, in one command.

Run it after bumping SIM_REVISION (src/game/SimRevision.h) or any change that moves
the golden match, in the same change:

    python3 tools/refresh_sim_fixtures.py

Steps, stopping at the first failure:

1. Build the dev tier (tools/dev_build.py): the game and engine tests.
   Same-platform checksums match between dev, perf and release builds, so the
   dev build is a valid fixture producer.
2. Rewrite the committed match record and verification trace
   (TurnEngineHarness), then every other [golden] doctest fixture.
3. Regenerate the telemetry simulation traces and the Maxima save-continuation
   baseline (serial runs).
4. Check the result with test/check_sim_revision.py --base <base>; this fails
   while the match record still names the previous sim version.

Map generators keep no committed rows: the sim-diff workflow compares every
generator's fingerprint between the merge base and the pull request.
"""
from __future__ import annotations

import argparse
import os
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
EXECUTABLE = '.exe' if os.name == 'nt' else ''


def dev_build_directory() -> Path:
    # build_identity imports scons/dev_build.py lazily; keep scons/ first on the path
    # until it returns, or tools/dev_build.py (this script's directory) shadows it.
    sys.path.insert(0, str(ROOT / 'scons'))
    try:
        from build_layout import build_identity, default_directory
        return ROOT / default_directory(build_identity({'dev_fast': '1', 'linker': 'auto'}))
    finally:
        sys.path.pop(0)


def run(step: str, command: list[str], **kwargs) -> None:
    print(f'\n== {step}\n$ {" ".join(str(c) for c in command)}', flush=True)
    result = subprocess.run([str(c) for c in command], cwd=ROOT, **kwargs)
    if result.returncode != 0:
        raise SystemExit(f'refresh_sim_fixtures: "{step}" failed with exit status {result.returncode}')


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--build-dir', type=Path, help='use this existing build instead of the dev build')
    parser.add_argument('--no-build', action='store_true', help='skip step 1 (the build must be current)')
    parser.add_argument('--base', default='origin/master', help='revision for the final sim-version check')
    parser.add_argument('-j', '--jobs', type=int, default=os.cpu_count() or 2)
    args = parser.parse_args(argv)

    build = (args.build_dir or dev_build_directory()).resolve()
    if not args.no_build and not args.build_dir:
        run('build the dev tier', [sys.executable, 'tools/dev_build.py', f'-j{args.jobs}',
                                   str((build / 'src/glob2').relative_to(ROOT)), 'engine-tests'])
    tests = [sys.executable, 'test/run_tests.py', '--build-dir', build, '--binary', 'engine', '-j', str(args.jobs)]
    run('committed match record and trace', tests + ['--update-fixtures', '--filter', 'TurnEngineHarness/the committed*'])
    run('[golden] doctest fixtures', tests + ['--update-fixtures', '--tag', 'golden'])
    glob2 = build / f'src/glob2{EXECUTABLE}'
    run('telemetry simulation traces', [sys.executable, 'test/check_telemetry_simulation.py', glob2, '--update-fixtures'])
    run('Maxima save continuation baseline', [sys.executable, 'test/maxima/check_save_continuation_fixture.py', glob2, '--update-fixtures'])
    run('sim version check', [sys.executable, 'test/check_sim_revision.py', '--base', args.base])
    print('\nrefresh_sim_fixtures: done; review `git status` and commit the regenerated fixtures with the change.')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
