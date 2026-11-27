#!/usr/bin/env python3
"""Crash a Linux glob2 built with Crashpad and check the crash is kept.

Starts the game with a scratch home directory and SDL's offscreen video driver,
waits for its first screen, sends it SIGSEGV, and checks that Crashpad wrote a
minidump; then starts it again and checks that the minidump moved to
reports/crashes/ (src/app/CrashCapture.h). With --symbols-root and
--stackwalk, it also symbolicates the dump and requires a frame of the game.

    python3 test/crash_capture_smoke.py build/linux/client/release/src/glob2
"""

import argparse
import os
from pathlib import Path
import signal
import subprocess
import sys
import tempfile
import time


def wait_for(predicate, seconds, what):
    deadline = time.monotonic() + seconds
    while time.monotonic() < deadline:
        if predicate():
            return
        time.sleep(0.5)
    raise SystemExit('Timed out waiting for ' + what)


def session_has_screen(reports):
    session = reports / 'session.log'
    return session.is_file() and b'Glob2 screen ready' in session.read_bytes()


def run(binary, home, log):
    environment = {name: value for name, value in os.environ.items()
                   if name not in ('DISPLAY', 'WAYLAND_DISPLAY')}
    environment.update(HOME=str(home), XDG_RUNTIME_DIR=str(home / 'runtime'), SDL_VIDEODRIVER='offscreen',
                       SDL_AUDIODRIVER='dummy')
    (home / 'runtime').mkdir(mode=0o700, exist_ok=True)
    return subprocess.Popen([str(binary)], env=environment, stdout=log, stderr=subprocess.STDOUT)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('binary', type=Path)
    parser.add_argument('--timeout', type=float, default=180)
    parser.add_argument('--symbols-root', type=Path, help='Breakpad symbol directory for --stackwalk')
    parser.add_argument('--stackwalk', type=Path, help='minidump-stackwalk')
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='glob2-crash-smoke-') as work:
        home = Path(work)
        reports = home / '.glob2/reports'
        with open(home / 'first.log', 'wb') as log:
            game = run(args.binary.resolve(), home, log)
            try:
                wait_for(lambda: session_has_screen(reports), args.timeout, 'the first screen')
                game.send_signal(signal.SIGSEGV)
                game.wait(timeout=60)
            finally:
                if game.poll() is None:
                    game.kill()
        dumps = sorted((reports / 'crashpad/pending').glob('*.dmp'))
        if not dumps:
            print((home / 'first.log').read_text(errors='replace')[-4000:])
            raise SystemExit('Crashpad wrote no minidump')
        print('Crashpad minidump:', dumps[0].name, dumps[0].stat().st_size, 'bytes')
        with open(home / 'second.log', 'wb') as log:
            game = run(args.binary.resolve(), home, log)
            try:
                wait_for(lambda: (reports / 'previous-session.log').is_file() and session_has_screen(reports),
                         args.timeout, 'the second run')
            finally:
                game.terminate()
                try:
                    game.wait(timeout=30)
                except subprocess.TimeoutExpired:
                    game.kill()
        kept = sorted((reports / 'crashes').glob('*.dmp'))
        if len(kept) != 1 or list((reports / 'crashpad/pending').glob('*.dmp')):
            raise SystemExit('The next run did not move the minidump to reports/crashes/')
        print('Kept for the consent prompt:', kept[0].name)
        if args.stackwalk and args.symbols_root:
            output = subprocess.run([str(args.stackwalk), '--symbols-path', str(args.symbols_root), str(kept[0])],
                                    capture_output=True, text=True, check=True).stdout
            frames = [line for line in output.splitlines() if 'glob2!' in line]
            if not frames:
                raise SystemExit('No symbolicated glob2 frame in the minidump:\n' + output[:4000])
            print('Symbolicated:', frames[0].strip())
    return 0


if __name__ == '__main__':
    sys.exit(main())
