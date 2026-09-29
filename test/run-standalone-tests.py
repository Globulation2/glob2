#!/usr/bin/env python3
"""Run every built standalone test, even when an earlier executable fails."""

from pathlib import Path
import os
import subprocess
import sys


def main():
    tests = [Path('TestsRunner'), *sorted(Path('.').glob('*Harness')),
             *sorted(Path('.').glob('*Test'))]
    failures = []
    for test in tests:
        print(f'== {test}', flush=True)
        result = subprocess.run([str(test.resolve())])
        if result.returncode:
            failures.append((str(test), result.returncode))
    if failures:
        print('Failed standalone tests: ' + ', '.join(
            f'{name} ({code})' for name, code in failures), flush=True)
        summary = os.environ.get('GITHUB_STEP_SUMMARY')
        if summary:
            with open(summary, 'a', encoding='utf-8') as output:
                output.write('### Failed standalone tests\n\n')
                for name, code in failures:
                    output.write(f'- {name}: exit {code}\n')
    return bool(failures)


if __name__ == '__main__':
    sys.exit(main())
