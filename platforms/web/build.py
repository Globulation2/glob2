#!/usr/bin/env python3
"""Compatibility entry point: a release browser build through tools/dev_build.py (cmake/web.cmake)."""
import os
from pathlib import Path
import subprocess
import sys
os.chdir(Path(__file__).resolve().parent.parent.parent)
raise SystemExit(subprocess.call([sys.executable, 'tools/dev_build.py', 'target=web', 'release=1',
                                  '-j' + os.environ.get('JOBS', '8'), *sys.argv[1:]]))
