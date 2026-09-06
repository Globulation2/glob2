#!/usr/bin/env python3
"""Run the hunger/defeat regression with a disposable profile."""
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import uuid

binary = Path(sys.argv[1]).resolve()
profile = "glob2-hunger-test-" + uuid.uuid4().hex
try:
    with tempfile.TemporaryDirectory(prefix=profile) as work:
        subprocess.run([str(binary), profile], cwd=work, check=True, timeout=90)
finally:
    if os.name != "nt":
        shutil.rmtree(Path.home() / ("." + profile), ignore_errors=True)
