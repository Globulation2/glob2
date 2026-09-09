#!/usr/bin/env python3
"""Run a headless regression in a disposable profile and working directory."""
import argparse
import difflib
import os
from pathlib import Path
import subprocess
import tempfile
import uuid

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--check-preferences', action='store_true')
parser.add_argument('--expect-stdout', type=Path)
parser.add_argument('binary', type=Path)
parser.add_argument('extra', nargs=argparse.REMAINDER)
args = parser.parse_args()
binary = args.binary.resolve()
extra = [arg if arg.startswith('--') else str(Path(arg).resolve()) for arg in args.extra]
expected = args.expect_stdout.read_text() if args.expect_stdout else None
profile = 'glob2-save-test-' + uuid.uuid4().hex
with tempfile.TemporaryDirectory(prefix=profile) as directory:
    root = Path(directory)
    home, work = root / 'home', root / 'work'
    home.mkdir()
    work.mkdir()
    preferences_dir = work if os.name == 'nt' else home / ('.' + profile)
    preferences_dir.mkdir(exist_ok=True)
    preferences = preferences_dir / 'preferences.txt'
    if args.check_preferences:
        preferences.write_text('rememberUnit=1\n')
        before = preferences.read_bytes(), preferences.stat().st_mtime_ns
    env = dict(os.environ, HOME=str(home), USERPROFILE=str(home))
    result = subprocess.run([str(binary), profile, *extra], cwd=work, env=env,
                            check=True, timeout=120, text=True,
                            stdout=subprocess.PIPE if expected is not None else None)
    if expected is not None:
        if result.stdout != expected:
            raise AssertionError(''.join(difflib.unified_diff(
                expected.splitlines(keepends=True), result.stdout.splitlines(keepends=True),
                fromfile=str(args.expect_stdout), tofile='actual stdout')))
        print(f'PASS: stdout matches {args.expect_stdout.name}')
    if args.check_preferences:
        assert (preferences.read_bytes(), preferences.stat().st_mtime_ns) == before, 'harness changed preferences'
        print('PASS: disposable profile preferences unchanged')
