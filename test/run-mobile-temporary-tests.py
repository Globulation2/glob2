#!/usr/bin/env python3
"""Run temporary-file cleanup checks under a disposable worktree profile."""
from pathlib import Path
import subprocess
import sys
import tempfile
root = Path(__file__).resolve().parents[1]
scratch = root/'build/test-profiles'
scratch.mkdir(parents=True, exist_ok=True)
with tempfile.TemporaryDirectory(prefix='cleanup-', dir=scratch) as profile:
    subprocess.run([str(Path(sys.argv[1]).resolve()), profile], check=True)
print('PASS: abandoned save/export cleanup preserves active writes, generations and symlink targets')
