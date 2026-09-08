#!/usr/bin/env python3
"""Compatibility entry point; all build logic lives in SCons."""
import os
from pathlib import Path
import subprocess
import sys
os.chdir(Path(__file__).resolve().parent.parent)
raise SystemExit(subprocess.call(['scons', 'target=web', 'release=1', '-j'+os.environ.get('JOBS','8'), *sys.argv[1:]]))
