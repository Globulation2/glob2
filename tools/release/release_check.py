#!/usr/bin/env python3
"""Run the release check (docs/releases/release-check.md) on a desktop build.

Starts the game with GLOB2_RELEASE_CHECK set, a scratch profile
(GLOB2_USER_DATA_DIR), dummy audio and, on Linux, SDL's offscreen video, and waits for
"Glob2 release check passed". The check plays the main menu, a custom game,
loading a save, a lesson, the tutorial and a saved game to its victory screen,
opens every results view and saves the replay (src/app/ReleaseCheck.h).

It fails when the game reports a failure, exits otherwise, crashes or runs out
of time. Any crash the game kept (reports/crashes/) and the game's output are
copied to --output, for the release job to upload as an artifact.

    python3 tools/release/release_check.py build/linux/client/release/src/glob2
"""

import argparse
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import time

PASSED = 'Glob2 release check passed'
FAILED = 'Glob2 release check failed'


def environment(profile, steps, base=None):
    """The game's environment: the check, a scratch profile, no display or audio device."""
    env = dict(os.environ if base is None else base)
    for name in ('DISPLAY', 'WAYLAND_DISPLAY'):
        env.pop(name, None)
    env.update(GLOB2_RELEASE_CHECK=steps, GLOB2_USER_DATA_DIR=str(profile), SDL_AUDIODRIVER='dummy')
    # Linux runners have no display; Windows and macOS runners have a desktop session.
    if sys.platform.startswith('linux'):
        env['SDL_VIDEODRIVER'] = 'offscreen'
    if os.name != 'nt':
        runtime = Path(profile) / 'runtime'
        runtime.mkdir(mode=0o700, parents=True, exist_ok=True)
        env['XDG_RUNTIME_DIR'] = str(runtime)
    return env


def verdict(output, returncode):
    """None when passed, otherwise why the check failed."""
    for line in output.splitlines():
        if FAILED in line:
            return line[line.index(FAILED):]
    if PASSED in output and returncode == 0:
        return None
    if PASSED in output:
        return f'the game exited with {returncode} after passing'
    return f'the game exited with {returncode} before passing'


def keep(profile, output_dir, log):
    """Copies the game's output and any crash it kept into output_dir."""
    output_dir.mkdir(parents=True, exist_ok=True)
    (output_dir / 'release-check.log').write_text(log)
    crashes = sorted((Path(profile) / 'reports/crashes').glob('*')) + \
        sorted((Path(profile) / 'reports/crashpad/pending').glob('*.dmp'))
    for crash in crashes:
        shutil.copy2(crash, output_dir / crash.name)
    return crashes


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('binary', type=Path)
    parser.add_argument('--steps', default='all', help='comma-separated steps, or all')
    parser.add_argument('--timeout', type=float, default=1800)
    parser.add_argument('--output', type=Path, default=Path('artifacts/release-check'))
    parser.add_argument('--data-dir', action='append', default=[], help='extra --data-dir for the game')
    args = parser.parse_args(argv)
    with tempfile.TemporaryDirectory(prefix='glob2-release-check-') as work:
        profile = Path(work) / 'profile'
        profile.mkdir()
        command = [str(args.binary.resolve())]
        for directory in args.data_dir:
            command += ['--data-dir', directory]
        started = time.monotonic()
        try:
            run = subprocess.run(command, env=environment(profile, args.steps), capture_output=True, text=True,
                                 errors='replace', timeout=args.timeout)
            output, code = run.stdout + run.stderr, run.returncode
        except subprocess.TimeoutExpired as expired:
            output = (expired.stdout or b'').decode(errors='replace') + (expired.stderr or b'').decode(errors='replace')
            code = None
        problem = verdict(output, code) if code is not None else f'timed out after {args.timeout:.0f} s'
        crashes = keep(profile, args.output, output)
        for line in output.splitlines():
            if 'Glob2 release check' in line:
                print(line[line.index('Glob2 release check'):])
        print(f'Release check took {time.monotonic() - started:.0f} s')
        if problem:
            print(output[-6000:])
            for crash in crashes:
                print('Kept crash:', args.output / crash.name)
            print('Release check failed:', problem, file=sys.stderr)
            return 1
    return 0


if __name__ == '__main__':
    sys.exit(main())
