#!/usr/bin/env python3
"""Run independent CI commands, reporting every failure after all have finished."""

import argparse
import os
import shutil
import subprocess
import sys


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--check', action='append', nargs=2, metavar=('NAME', 'COMMAND'), required=True)
    args = parser.parse_args(argv)
    bash = shutil.which('bash')
    if not bash:
        parser.error('bash is required to run CI commands')
    failed = []
    for name, command in args.check:
        print(f'::group::{name}', flush=True)
        result = subprocess.run([bash, '-c', command])
        print('::endgroup::', flush=True)
        if result.returncode:
            failed.append((name, result.returncode))
            print(f'::error title={name} failed::{name} exited {result.returncode}', flush=True)
    if failed:
        print('Failed checks: ' + ', '.join(f'{name} ({code})' for name, code in failed), flush=True)
        summary = os.environ.get('GITHUB_STEP_SUMMARY')
        if summary:
            with open(summary, 'a', encoding='utf-8') as output:
                output.write('### Failed commands\n\n')
                for name, code in failed:
                    output.write(f'- {name}: exit {code}\n')
    return bool(failed)


if __name__ == '__main__':
    sys.exit(main())
