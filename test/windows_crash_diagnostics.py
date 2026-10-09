#!/usr/bin/env python3
"""Replay Windows native crashes under GDB without changing their CI result."""
import argparse
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile
import xml.etree.ElementTree as ET

from run_tests import doctest_pattern


def crashed_cases(document):
    for case in document.iter('testcase'):
        for error in case.findall('error'):
            match = re.search(r'exit status (-?\d+)', error.text or '')
            # The Windows CRT reports abort()/failed C assertions as status 3.
            if match and (int(match[1]) == 3 or
                          (int(match[1]) & 0xFFFFFFFF) >= 0xC0000000):
                yield case.get('classname', ''), case.get('name', '')
                break


def debugger_command(debugger, binary, suite, name):
    return [debugger, '--nx', '--batch', '--quiet', '-ex', 'set pagination off',
            '-ex', 'run', '-ex', 'thread apply all bt', '--args', str(binary),
            '--no-breaks=true', '-tc=' + doctest_pattern(name), '-ts=' + doctest_pattern(suite)]


def print_diagnostic(text, stream=None):
    stream = sys.stdout if stream is None else stream
    encoding = stream.encoding or 'utf-8'
    # Keep the raw bytes in the artifact; Windows consoles may use cp1252.
    stream.write(text.encode(encoding, errors='backslashreplace').decode(encoding) + '\n')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', type=Path, required=True)
    parser.add_argument('--junit', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    debugger = shutil.which('gdb')
    if not debugger:
        print('Crash diagnostics unavailable: GDB is not installed.')
        return
    cases = list(crashed_cases(ET.parse(args.junit).getroot()))
    args.output.mkdir(parents=True, exist_ok=True)
    for index, (suite, name) in enumerate(cases):
        # Each replay has its own profile and artifact directory. The original
        # crash evidence and original failed result remain intact.
        with tempfile.TemporaryDirectory(prefix='glob2-crash-') as profile:
            env = os.environ.copy()
            env.update(GLOB2_USER_DATA_DIR=profile, SDL_VIDEODRIVER='dummy',
                       SDL_RENDER_DRIVER='software', SDL_AUDIODRIVER='dummy',
                       GLOB2_TEST_ARTIFACTS=str((args.output / str(index)).resolve()))
            command = debugger_command(debugger, args.binary.resolve(), suite, name)
            try:
                result = subprocess.run(command, env=env, stdout=subprocess.PIPE,
                                        stderr=subprocess.STDOUT, timeout=180)
                output = result.stdout
            except subprocess.TimeoutExpired as error:
                output = (error.stdout or b'') + b'\nCrash diagnostic replay timed out.\n'
            log = args.output / f'crash-{index}.log'
            log.write_bytes(output)
            print_diagnostic(f'{suite}/{name}: {log}')
            print_diagnostic(output.decode('utf-8', errors='replace'))


if __name__ == '__main__':
    main()
