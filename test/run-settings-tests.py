#!/usr/bin/env python3
"""Native settings integration and visual captures; never uses the real profile.
Build with: scons -j6 release=1 settings-tests
"""
import argparse
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import uuid

root = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser()
parser.add_argument('--quick', action='store_true')
args = parser.parse_args()
cases = [(1000, 700, 'gl', False)] if args.quick else [(640, 480, 'gl', False), (800, 600, 'gl', False), (1000, 700, 'gl', False), (1280, 900, 'gl', False), (1000, 700, 'software', False), (640, 480, 'gl', True)]
for width, height, renderer, expanded in cases:
    profile = 'glob2-settings-test-' + uuid.uuid4().hex
    output = root / 'artifacts' / 'settings-redesign' / (f'{width}x{height}-{renderer}' + ('-expanded' if expanded else ''))
    output.mkdir(parents=True, exist_ok=True)
    try:
        if expanded:
            # Override only the disposable profile's English strings.
            destination = Path.home()/('.'+profile)/'data'/'texts.en.txt'
            destination.parent.mkdir(parents=True, exist_ok=True)
            lines = (root/'data/texts.en.txt').read_text().splitlines()
            for i in range(0, len(lines)-1, 2):
                if lines[i].startswith('[settings ') and len(lines[i+1]) > 12:
                    lines[i+1] += ' — ' + lines[i+1]
            destination.write_text('\n'.join(lines)+'\n')
        with tempfile.TemporaryDirectory(prefix=profile) as work:
            result = subprocess.run([str(root/'build/src/settings-screen-tests'), profile, str(width), str(height), renderer, str(output)],
                cwd=work, env=dict(os.environ, SDL_AUDIODRIVER='dummy'), timeout=120,
                stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
            (output/'test.log').write_text(result.stdout)
            print(f'{width}x{height} {renderer}: exit {result.returncode}')
            print('\n'.join(result.stdout.splitlines()[-8:]))
            if result.returncode:
                raise SystemExit(result.returncode)
    finally:
        shutil.rmtree(Path.home()/('.'+profile), ignore_errors=True)
