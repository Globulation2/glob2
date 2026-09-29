#!/usr/bin/env python3
"""Report failed GitHub Actions steps and fail the job after independent checks run."""

import json
import os
import sys


def failed_steps(steps):
    return [name for name, state in steps.items()
            if isinstance(state, dict) and state.get('outcome') == 'failure']


def main():
    failures = failed_steps(json.loads(os.environ['CI_STEPS_JSON']))
    summary = os.environ.get('GITHUB_STEP_SUMMARY')
    lines = ['## CI check results', '']
    if failures:
        lines += [f'- Failed: `{name}`' for name in failures]
    else:
        lines.append('All completed checks passed.')
    report = '\n'.join(lines) + '\n'
    print(report)
    if summary:
        with open(summary, 'a', encoding='utf-8') as output:
            output.write(report)
    return bool(failures)


if __name__ == '__main__':
    sys.exit(main())
