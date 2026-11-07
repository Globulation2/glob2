#!/usr/bin/env python3
"""The master-red rule for the fast gate.

When the newest completed full master run (build.yml on master) has failed and
master has had no successful full run for more than 24 hours, the next merge must
repair or revert the breakage: pull requests fail this check unless they carry the
`ci:repair` label. Master pushes and manual runs only report the state.

Reads GH_TOKEN, REPOSITORY and LABELS (a JSON list) from the environment.
"""
from __future__ import annotations

import datetime as dt
import json
import os
import subprocess
import sys

WORKFLOW = 'build.yml'
LIMIT = dt.timedelta(hours=24)
REPAIR_LABEL = 'ci:repair'


def parse_time(text: str) -> dt.datetime:
    return dt.datetime.fromisoformat(text.replace('Z', '+00:00'))


def master_runs(repository: str) -> list[dict]:
    output = subprocess.check_output([
        'gh', 'api', f'repos/{repository}/actions/workflows/{WORKFLOW}/runs?branch=master&status=completed&per_page=50',
        '--jq', '.workflow_runs | map({conclusion, created_at, html_url, event})'], text=True)
    return [run for run in json.loads(output) if run['event'] in ('push', 'schedule')]


def red_since(runs: list[dict]) -> dt.datetime | None:
    """When master turned red, or None when the newest decided run passed.

    Cancelled and skipped runs decide nothing; the newest success or failure does."""
    decided = [run for run in runs if run['conclusion'] in ('success', 'failure')]
    if not decided or decided[0]['conclusion'] == 'success':
        return None
    first_failure = decided[0]
    for run in decided:
        if run['conclusion'] == 'success':
            break
        first_failure = run
    return parse_time(first_failure['created_at'])


def verdict(runs: list[dict], labels: list[str], now: dt.datetime) -> tuple[bool, str]:
    since = red_since(runs)
    if since is None:
        return True, 'master is green (newest decided full run passed)'
    age = now - since
    hours = age.total_seconds() / 3600
    if age <= LIMIT:
        return True, f'master has been red for {hours:.1f} h (limit 24 h)'
    if REPAIR_LABEL in labels:
        return True, f'master has been red for {hours:.1f} h; this pull request is labelled {REPAIR_LABEL}'
    return False, (f'master has been red for {hours:.1f} h. The next merge must repair or revert the '
                   f'breakage: label the repair pull request {REPAIR_LABEL}. Newest run: {runs[0]["html_url"] if runs else "?"}')


def main() -> int:
    labels = json.loads(os.environ.get('LABELS') or '[]') or []
    try:
        runs = master_runs(os.environ['REPOSITORY'])
    except (subprocess.CalledProcessError, KeyError, ValueError) as error:
        print(f'::warning::cannot read master CI state ({error}); not enforcing the master-red rule')
        return 0
    ok, message = verdict(runs, labels, dt.datetime.now(dt.timezone.utc))
    print(message if ok else f'::error::{message}')
    return 0 if ok or os.environ.get('GITHUB_EVENT_NAME') != 'pull_request' else 1


if __name__ == '__main__':
    sys.exit(main())
