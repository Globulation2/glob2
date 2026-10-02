#!/usr/bin/env python3
"""Run a display test with an EWMH window manager inside an owned Xvfb session."""
import re
import shutil
import subprocess
import sys
import time


def run(command):
    if not command:
        raise ValueError('a test command is required')
    manager = shutil.which('openbox')
    xprop = shutil.which('xprop')
    if not manager or not xprop:
        raise RuntimeError('Linux display tests require openbox and x11-utils alongside xvfb')
    # Keep children in the runner's process group so timeout cleanup also kills
    # the window manager. Each xvfb-run invocation owns its separate display.
    wm = subprocess.Popen([manager, '--sm-disable'], stdout=subprocess.DEVNULL,
                          stderr=None)
    try:
        deadline = time.monotonic() + 5
        while True:
            if wm.poll() is not None:
                raise RuntimeError(f'Openbox exited before readiness with status {wm.returncode}')
            state = subprocess.run([xprop, '-root', '_NET_SUPPORTING_WM_CHECK'],
                                   capture_output=True, text=True, timeout=2)
            match = re.search(r'window id #\s*(0x[0-9a-fA-F]+)', state.stdout)
            if state.returncode == 0 and match and int(match.group(1), 16):
                break
            if time.monotonic() >= deadline:
                raise RuntimeError('Openbox did not establish an EWMH window manager')
            time.sleep(0.05)
        return subprocess.call(command)
    finally:
        wm.terminate()
        try:
            wm.wait(timeout=5)
        except subprocess.TimeoutExpired:
            wm.kill()
            wm.wait()


if __name__ == '__main__':
    try:
        sys.exit(run(sys.argv[1:]))
    except (ValueError, RuntimeError, subprocess.TimeoutExpired) as error:
        print(f'Xvfb display session: {error}', file=sys.stderr)
        sys.exit(1)
