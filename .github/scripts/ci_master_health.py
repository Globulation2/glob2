#!/usr/bin/env python3
"""The master-red rule for the fast gate.

A full master run (build.yml on master, push or schedule) is judged only by its
gating jobs (GATING_JOBS): it is red when one of them failed or timed out, green
when every gating job that ran passed, and undecided otherwise (a cancelled
gating job, or none present, decides nothing). Coverage, sanitizer and evidence
jobs are not gating: they report regressions without making master red.

When the newest decided run is red and master has had no green decided run for
more than 24 hours, the next merge must repair or revert the breakage: pull
requests carry the `ci:repair` label. While ENFORCED is False the rule only warns;
re-enable it once master has stayed green for about two weeks.

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
# Advisory until master has stayed green for about 14 days (see "Master red rule"
# in docs/development/verification.md); then set True in its own pull request.
ENFORCED = False
# Display-name prefixes of the jobs whose result decides whether master is red.
GATING_JOBS = (
    'Linux GCC 11 build and tests /',
    'Linux GCC 13 build and tests /',
    'linux-build',
    'windows',
    'macos / native',
    'android /',
    'browser build',
    'browser tests (',
)
FAILED = ('failure', 'timed_out')


def parse_time(text: str) -> dt.datetime:
    return dt.datetime.fromisoformat(text.replace('Z', '+00:00'))


def gating(name: str) -> bool:
    return any(name == prefix or name.startswith(prefix) for prefix in GATING_JOBS)


def run_state(jobs: list[dict]) -> str:
    """'failure', 'success' or 'undecided' from a run's jobs."""
    gates = [job for job in jobs if gating(job['name'])]
    if any(job['conclusion'] in FAILED for job in gates):
        return 'failure'
    if gates and all(job['conclusion'] in ('success', 'skipped') for job in gates) \
            and any(job['conclusion'] == 'success' for job in gates):
        return 'success'
    return 'undecided'


def master_runs(repository: str) -> list[dict]:
    output = subprocess.check_output([
        'gh', 'api', f'repos/{repository}/actions/workflows/{WORKFLOW}/runs?branch=master&status=completed&per_page=30',
        '--jq', '.workflow_runs | map({id, created_at, html_url, event})'], text=True)
    runs = [run for run in json.loads(output) if run['event'] in ('push', 'schedule')]
    for run in runs:
        jobs = subprocess.check_output([
            'gh', 'api', '--paginate', f'repos/{repository}/actions/runs/{run["id"]}/jobs?per_page=100',
            '--jq', '.jobs[] | {name, conclusion}'], text=True)
        run['state'] = run_state([json.loads(line) for line in jobs.splitlines() if line.strip()])
    return runs


def red_since(runs: list[dict]) -> dt.datetime | None:
    """When master turned red, or None when the newest decided run passed."""
    decided = [run for run in runs if run['state'] in ('success', 'failure')]
    if not decided or decided[0]['state'] == 'success':
        return None
    first_failure = decided[0]
    for run in decided:
        if run['state'] == 'success':
            break
        first_failure = run
    return parse_time(first_failure['created_at'])


def verdict(runs: list[dict], labels: list[str], now: dt.datetime) -> tuple[bool, str]:
    since = red_since(runs)
    if since is None:
        return True, 'master is green (newest decided full run passed its gating jobs)'
    age = now - since
    hours = age.total_seconds() / 3600
    if age <= LIMIT:
        return True, f'master has been red for {hours:.1f} h (limit 24 h)'
    if REPAIR_LABEL in labels:
        return True, f'master has been red for {hours:.1f} h; this pull request is labelled {REPAIR_LABEL}'
    newest = next((run for run in runs if run['state'] == 'failure'), None)
    return False, (f'master has been red for {hours:.1f} h. The next merge must repair or revert the '
                   f'breakage: label the repair pull request {REPAIR_LABEL}. Newest red run: '
                   f'{newest["html_url"] if newest else "?"}')


def main() -> int:
    labels = json.loads(os.environ.get('LABELS') or '[]') or []
    try:
        runs = master_runs(os.environ['REPOSITORY'])
    except (subprocess.CalledProcessError, KeyError, ValueError) as error:
        print(f'::warning::cannot read master CI state ({error}); not enforcing the master-red rule')
        return 0
    ok, message = verdict(runs, labels, dt.datetime.now(dt.timezone.utc))
    if ok:
        print(message)
        return 0
    if not ENFORCED:
        print(f'::warning::{message} (advisory: the rule is not enforced yet)')
        return 0
    print(f'::error::{message}')
    return 1 if os.environ.get('GITHUB_EVENT_NAME') == 'pull_request' else 0


if __name__ == '__main__':
    sys.exit(main())
