#!/usr/bin/env python3
"""Run from any directory after `scons release=1 speed-tests`.

Requires a display with OpenGL (on Linux CI, use xvfb-run) and game data.
All preferences, saves and replays go into a disposable profile.
"""
import os
from pathlib import Path
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
        )
    raise SystemExit(result.returncode)
finally:
    if os.name != 'nt':
        shutil.rmtree(Path.home() / ('.' + profile), ignore_errors=True)
