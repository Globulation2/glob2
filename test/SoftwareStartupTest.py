#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Smoke-test a software-only game with GPU mode requested, without a display."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile

executable = Path(sys.argv[1]).resolve()
repository = Path(__file__).resolve().parent.parent
with tempfile.TemporaryDirectory(prefix="glob2-software-smoke-") as profile:
    environment = dict(os.environ, GLOB2_USER_DIR=profile,
                       SDL_VIDEODRIVER="dummy", SDL_AUDIODRIVER="dummy")
    try:
        result = subprocess.run(
            [str(executable), "-g", "-F", "-m", "-s", "640x480"],
            cwd=repository, env=environment, stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT, text=True, timeout=5)
    except subprocess.TimeoutExpired:
        # run() kills and waits for the child on timeout. The interactive game
        # should remain running until then; startup failures exit immediately.
        print("Software-only startup with GPU mode requested passed")
    else:
        print(result.stdout)
        sys.exit("Game exited unexpectedly during software startup: " + str(result.returncode))
