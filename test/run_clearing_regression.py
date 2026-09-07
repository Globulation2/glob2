#!/usr/bin/env python3
"""Run the SCons-built clearing regression with an isolated user profile."""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--build-dir', type=Path, required=True)
args = parser.parse_args()
build = (ROOT / args.build_dir).resolve()
binary = build / 'src' / ('ClearingFlagGradientTest.exe' if os.name == 'nt' else 'ClearingFlagGradientTest')
if not binary.is_file():
    parser.error('Build the clearing-gradient-test SCons target first: ' + str(binary))
with tempfile.TemporaryDirectory(prefix='glob2-clearing-') as temp:
    root = Path(temp)
    home = root / 'home'
    work = root / 'work'
    home.mkdir()
    work.mkdir()
    profile = work if os.name == 'nt' else home / '.glob2-clearing-regression'
    profile.mkdir(exist_ok=True)
    preferences = profile / 'preferences.txt'
    preferences.write_text('rememberUnit=1\n')
    before = (preferences.read_bytes(), preferences.stat().st_mtime_ns)
    env = dict(os.environ, HOME=str(home), USERPROFILE=str(home))
    subprocess.run([str(binary)], cwd=work, env=env, check=True, timeout=90)
    assert (preferences.read_bytes(), preferences.stat().st_mtime_ns) == before
    print('PASS: disposable profile preferences unchanged')
