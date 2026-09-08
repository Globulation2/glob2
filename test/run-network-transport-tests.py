#!/usr/bin/env python3
"""Run shared framing and native loopback tests after `scons release=1 transport-test`."""
from pathlib import Path
import os
import platform
import socket
import subprocess

root = Path(__file__).resolve().parents[1]
build = os.environ.get('GLOB2_BUILD_DIR', 'build/' + platform.system().lower() + '/client/release')
with socket.socket() as listener:
    listener.bind(('127.0.0.1', 0))
    port = listener.getsockname()[1]
raise SystemExit(subprocess.run(
    [str(root / build / 'src/net-connection-test'), str(port)], cwd=root, timeout=15,
).returncode)
