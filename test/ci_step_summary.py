#!/usr/bin/env python3
"""Report failed GitHub Actions steps and fail the job after independent checks run."""

import json
import os
from pathlib import Path
import sys


def failed_steps(steps):
    return [name for name, state in steps.items()
            if isinstance(state, dict) and state.get('outcome') == 'failure']


def main():
    steps = json.loads(os.environ['CI_STEPS_JSON'])
    failures = failed_steps(steps)
    caches = {name: state.get('outputs', {}).get('cache-hit') for name, state in steps.items()
              if 'cache-hit' in state.get('outputs', {})}
    evidence = Path('artifacts/ci-observation/cache.json')
    evidence.parent.mkdir(parents=True, exist_ok=True)
    evidence.write_text(json.dumps({'job': os.environ.get('GITHUB_JOB'), 'caches': caches}) + '\n')
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
