#!/usr/bin/env python3
"""Cancel CI runs whose commit is no longer the head of their pull request or branch.

Workflow-level ``concurrency`` only replaces runs of the *same* workflow. This
covers the remainder: a path-filtered workflow that a newer push no longer
triggers, runs left behind by a closed pull request, and stale runs of one
workflow when another workflow started for the new head.

Only event-triggered runs of the CI workflows in ``CI_WORKFLOWS`` are ever
cancelled. Release, publication and deployment workflows, manual dispatches and
the calling run are never touched. The default branch is skipped unless
``--include-default-branch`` is given, and even then its newest run of each
workflow is kept.
"""
import argparse
import json
import os
import subprocess
import sys
from urllib.parse import quote

# Workflows whose push/pull_request runs are disposable verification. Anything
# not listed here (release, publish, deploy, metrics) is never cancelled.
CI_WORKFLOWS = {
    '.github/workflows/build.yml',
    '.github/workflows/mac-app-store.yml',
    '.github/workflows/mobile.yml',
    '.github/workflows/steam-windows-package.yml',
    '.github/workflows/thread-sanitizer.yml',
}
CANCELLABLE_EVENTS = {'pull_request', 'push'}
DEFAULT_BRANCH_EVENTS = {'push'}
ACTIVE_STATUSES = ('queued', 'in_progress', 'waiting', 'pending', 'requested')


def workflow_path(run):
    # Runs of a workflow from another ref report "path@ref".
    return (run.get('path') or '').split('@', 1)[0]


def head_key(run):
    """Pull request runs follow the PR head; other runs follow their branch."""
    repo = (run.get('head_repository') or {}).get('full_name')
    kind = 'pr' if run.get('event') == 'pull_request' else 'branch'
    return kind, repo, run.get('head_branch')


def select_superseded(runs, heads, current_run_id=None, default_branch='master', repository=None,
                      include_default_branch=False):
    """Return the runs to cancel.

    ``heads`` maps ``head_key`` values to the commit that is current
    for that branch, or ``None`` when nothing should run for it any more (the
    pull request closed or the branch was deleted). Branches absent from
    ``heads`` are unknown and left alone.
    """
    selected = []
    newest_default = {}
    for run in runs:
        if run.get('status') == 'completed' or str(run.get('id')) == str(current_run_id):
            continue
        if workflow_path(run) not in CI_WORKFLOWS:
            continue
        key = head_key(run)
        on_default = key[1:] == (repository, default_branch) and run.get('event') in DEFAULT_BRANCH_EVENTS
        if on_default:
            if not include_default_branch or run.get('status') == 'in_progress':
                continue
            path = workflow_path(run)
            if path not in newest_default or run['id'] > newest_default[path]['id']:
                newest_default[path] = run
        elif run.get('event') not in CANCELLABLE_EVENTS:
            continue
        if key not in heads:
            continue
        head = heads[key]
        if head is not None and run.get('head_sha') == head:
            continue
        selected.append(run)
    keep = {run['id'] for run in newest_default.values()}
    return [run for run in selected if run['id'] not in keep]


def gh_json(*args):
    result = subprocess.run(['gh', 'api', *args], check=True, capture_output=True, text=True)
    return json.loads(result.stdout) if result.stdout.strip() else None


def gh_pages(endpoint, key):
    items = []
    page = 1
    while True:
        data = gh_json(f'{endpoint}{"&" if "?" in endpoint else "?"}per_page=100&page={page}')
        batch = data[key] if isinstance(data, dict) else data
        items.extend(batch)
        if len(batch) < 100:
            return items
        page += 1


def active_runs(repo, branch=None):
    runs = {}
    for status in ACTIVE_STATUSES:
        query = f'repos/{repo}/actions/runs?status={status}'
        if branch:
            query += f'&branch={quote(branch, safe="")}'
        for run in gh_pages(query, 'workflow_runs'):
            runs[run['id']] = run
    return list(runs.values())


def branch_head(repo, branch):
    try:
        return gh_json(f'repos/{repo}/branches/{quote(branch, safe="")}')['commit']['sha']
    except subprocess.CalledProcessError:
        # An unavailable API is not evidence that a branch was deleted.
        return False


def resolve_heads(repo, runs):
    """Current head per (head repository, branch) for the given runs."""
    open_prs = {}
    for pr in gh_pages(f'repos/{repo}/pulls?state=open', None):
        head_repo = (pr['head'].get('repo') or {}).get('full_name')
        key = ('pr', head_repo, pr['head']['ref'])
        open_prs[key] = pr['head']['sha']
    heads = {}
    for run in runs:
        key = head_key(run)
        if key in heads:
            continue
        if key[0] == 'pr':
            heads[key] = open_prs.get(key)  # closed or merged PR: nothing current
        elif key[1] == repo:
            value = branch_head(repo, key[2])
            if value is not False:
                heads[key] = value
    return heads


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--repo', default=os.environ.get('GITHUB_REPOSITORY'))
    parser.add_argument('--branch', help='Only consider runs for this head branch')
    parser.add_argument('--current-run-id', default=os.environ.get('GITHUB_RUN_ID'))
    parser.add_argument('--default-branch', default='master')
    parser.add_argument('--include-default-branch', action='store_true',
                        help='Also cancel superseded default-branch runs, keeping the newest per workflow')
    parser.add_argument('--dry-run', action='store_true')
    args = parser.parse_args()
    if not args.repo:
        parser.error('--repo or GITHUB_REPOSITORY is required')

    runs = active_runs(args.repo, args.branch)
    heads = resolve_heads(args.repo, runs)
    selected = select_superseded(runs, heads, args.current_run_id, args.default_branch, args.repo,
                                 args.include_default_branch)
    cancelled = 0
    for run in selected:
        label = f"{run['id']} {workflow_path(run)} {run['event']} {run['head_branch']}@{run['head_sha'][:9]} ({run['status']})"
        if args.dry_run:
            print(f'would cancel {label}')
            continue
        try:
            gh_json('-X', 'POST', f"repos/{args.repo}/actions/runs/{run['id']}/cancel")
            cancelled += 1
            print(f'cancelled {label}')
        except subprocess.CalledProcessError as error:
            # Already finished or not cancellable; never fail the caller for it.
            print(f'could not cancel {label}: {error.stderr.strip()}', file=sys.stderr)
    print(f'{cancelled if not args.dry_run else len(selected)} superseded run(s) '
          f'{"selected" if args.dry_run else "cancelled"}')


if __name__ == '__main__':
    main()
