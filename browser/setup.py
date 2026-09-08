#!/usr/bin/env python3
"""Install the exact SDK revision described by toolchain.json."""
import json
from pathlib import Path
import subprocess

root = Path(__file__).resolve().parent.parent
lock = json.loads((root / 'browser/toolchain.json').read_text())
sdk = root / 'tools/browser-emsdk'
if not sdk.exists():
    subprocess.run(['git', 'clone', 'https://github.com/emscripten-core/emsdk.git', str(sdk)], check=True)
subprocess.run(['git', '-C', str(sdk), 'checkout', '--detach', lock['emsdk_commit']], check=True)
for action in ('install','activate'):
    subprocess.run([str(sdk / 'emsdk'), action, lock['emscripten']], check=True)
