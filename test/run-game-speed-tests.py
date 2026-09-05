#!/usr/bin/env python3
"""Run from any directory after `scons release=1 speed-tests`.

Requires a display with OpenGL (on Linux CI, use xvfb-run) and game data.
All preferences, saves and replays go into a disposable profile.
"""
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import uuid

root = Path(__file__).resolve().parents[1]
profile = 'glob2-speed-test-' + uuid.uuid4().hex
try:
    with tempfile.TemporaryDirectory(prefix=profile) as work:
        result = subprocess.run(
            [str(root / 'build/src/game-speed-tests'), profile], cwd=work,
            env=dict(os.environ, SDL_AUDIODRIVER='dummy'), timeout=60,
            stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True,
        )
        print(result.stdout, end='')
        if result.returncode == 0:
            checksums = re.findall(r'nox::gui\.game\.checkSum\(\) = ([0-9a-f]+)', result.stdout)
            assert len(checksums) == 7, 'Missing engine checksums'
            assert len(set(checksums[:4])) == 1, 'Speed or pause changed the game state'
            assert len(set(checksums[4:])) == 1, 'Playback speed changed the replay state'
    raise SystemExit(result.returncode)
finally:
    if os.name != 'nt':
        shutil.rmtree(Path.home() / ('.' + profile), ignore_errors=True)
