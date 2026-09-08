#!/usr/bin/env python3
"""Run from any directory after `scons release=1 session-test`.

Uses SDL dummy software display; requires installed game data.
All preferences, saves and replays go into a disposable profile.
"""
import sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from build_paths import native_binary
import os
import platform
import re
import subprocess
import tempfile
import uuid

root = Path(__file__).resolve().parents[1]
profile = 'glob2-session-test-' + uuid.uuid4().hex
scratch = root / 'build/test-profiles'
scratch.mkdir(parents=True, exist_ok=True)
with tempfile.TemporaryDirectory(prefix=profile, dir=scratch) as work:
    result = subprocess.run(
        [str(native_binary('engine-session-test')), profile], cwd=work,
        env=dict(os.environ, SDL_AUDIODRIVER='dummy', GLOB2_USER_DATA_DIR=str(Path(work) / 'profile')), timeout=60,
        stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True,
    )
    print(result.stdout, end='')
    if result.returncode == 0:
        checksums = re.findall(r'nox::gui\.game\.checkSum\(\) = ([0-9a-f]+)', result.stdout)
        assert len(checksums) == 3, 'Missing session checksums'
        assert len(set(checksums)) == 1, 'Callback timing changed simulation state'
raise SystemExit(result.returncode)
