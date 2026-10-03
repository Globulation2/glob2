"""Reclaim closed-PR caches and obsolete measurement jobs; never cancel builds."""
import argparse
import re
from ci_cancel_superseded import gh_pages, gh_json


def obsolete_caches(caches, open_prs, generations=3):
    remove, families = [], {}
    for cache in caches:
        ref = cache['ref']
        match = re.fullmatch(r'refs/pull/(\d+)/merge', ref)
        if match and int(match[1]) not in open_prs:
            remove.append(cache)
        elif ref == 'refs/heads/master':
            key = cache['key']
            if key.startswith(('ccache-', 'linux-objects-', 'wasm-objects-', 'native-objects-')):
                family = re.sub(r'-\d{8,}-\d+$', '', key)
                families.setdefault(family, []).append(cache)
    for rows in families.values():
        remove.extend(sorted(rows, key=lambda row: row['created_at'], reverse=True)[generations:])
    return remove


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--repo', required=True)
    parser.add_argument('--dry-run', action='store_true')
    parser.add_argument('--measurements', action='store_true', help='one-time cleanup of legacy queued per-run metrics')
    args = parser.parse_args()
    prs = {pr['number'] for pr in gh_pages(f'repos/{args.repo}/pulls?state=open', None)}
    caches = gh_pages(f'repos/{args.repo}/actions/caches', 'actions_caches')
    for cache in obsolete_caches(caches, prs):
        print('delete cache', cache['id'], cache['ref'], cache['key'])
        if not args.dry_run:
            gh_json('-X', 'DELETE', f'repos/{args.repo}/actions/caches/{cache["id"]}')
    if args.measurements:
        runs = gh_pages(f'repos/{args.repo}/actions/workflows/ci-metrics.yml/runs?status=queued', 'workflow_runs')
        for run in runs:
            if run['event'] != 'workflow_run':
                continue
            print('cancel obsolete measurement', run['id'])
            if not args.dry_run:
                gh_json('-X', 'POST', f'repos/{args.repo}/actions/runs/{run["id"]}/cancel')


if __name__ == '__main__':
    main()
