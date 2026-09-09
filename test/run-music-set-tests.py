#!/usr/bin/env python3
"""Run the music integration harness with a disposable profile and dummy audio.

Build: scons --build=build-validation release=1 music-set-tests
Run: python3 test/run-music-set-tests.py
Requires a graphical display for the in-game Options screen.
"""
import os
from pathlib import Path
import shutil
import subprocess
import sys
import uuid

root = Path(__file__).resolve().parents[1]
binary = Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else root / 'build-validation/src/music-set-tests'
profile = 'glob2-music-test-' + uuid.uuid4().hex
try:
    result = subprocess.run([str(binary), profile], cwd=root,
                            env=dict(os.environ, SDL_AUDIODRIVER='dummy'),
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, timeout=90)
    print(result.stdout, end='')
    raise SystemExit(result.returncode)
finally:
    shutil.rmtree(Path.home() / ('.' + profile), ignore_errors=True)
