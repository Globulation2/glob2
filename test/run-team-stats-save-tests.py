#!/usr/bin/env python3
"""Run team-stats-save-test with disposable preferences and writable files."""
import argparse
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import uuid

ROOT = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('binary', type=Path)
parser.add_argument('--write-fixture', type=Path)
parser.add_argument('--legacy', type=Path)
args = parser.parse_args()
profile = 'glob2-stats-test-' + uuid.uuid4().hex
profile_dir = Path.home() / ('.' + profile)
try:
    with tempfile.TemporaryDirectory(prefix=profile) as directory:
        work = Path(directory)
        preferences_dir = work if os.name == 'nt' else profile_dir
        preferences_dir.mkdir(exist_ok=True)
        preferences = preferences_dir / 'preferences.txt'
        preferences.write_text('rememberUnit=1\n')
        before = preferences.read_bytes(), preferences.stat().st_mtime_ns
        command = [str(args.binary.resolve()), profile, str(ROOT)]
        if args.write_fixture:
            command += ['--write-fixture', str(args.write_fixture.resolve())]
        if args.legacy:
            command += ['--legacy', str(args.legacy.resolve())]
        subprocess.run(command, cwd=work, check=True, timeout=120)
        if not args.write_fixture and not args.legacy:
            for fixture, expected in (
                (ROOT / 'test/fixtures/team-stats/version88.game', ROOT / 'test/fixtures/team-stats/version88.expected.txt'),
                (ROOT / 'games/gd-small-2ai.game', ROOT / 'test/fixtures/team-stats/version84.expected.txt'),
            ):
                result = subprocess.run(command + ['--legacy', str(fixture)], cwd=work,
                                        check=True, timeout=120, capture_output=True, text=True)
                assert result.stdout == expected.read_text(), f'legacy statistics behavior changed: {fixture}'
                print(f'Legacy save trace matches upstream: {fixture.name}', flush=True)
        assert (preferences.read_bytes(), preferences.stat().st_mtime_ns) == before, 'harness changed preferences'
finally:
    if os.name != 'nt':
        shutil.rmtree(profile_dir, ignore_errors=True)
