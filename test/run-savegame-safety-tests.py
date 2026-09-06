#!/usr/bin/env python3
"""Run savegame-safety-test without a display, in a disposable profile."""
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import uuid

binary = Path(sys.argv[1]).resolve()
extra = [str(Path(arg).resolve()) for arg in sys.argv[2:]]
profile = 'glob2-save-test-' + uuid.uuid4().hex
try:
    with tempfile.TemporaryDirectory(prefix=profile) as work:
        subprocess.run([str(binary), profile, *extra], cwd=work, check=True, timeout=90)
finally:
    if os.name != 'nt':
        shutil.rmtree(Path.home() / ('.' + profile), ignore_errors=True)
