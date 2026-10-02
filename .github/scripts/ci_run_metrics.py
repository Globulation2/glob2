#!/usr/bin/env python3
"""Read-only CI measurements. Never execute downloaded artifacts or PR code."""
import argparse
from datetime import datetime
import io
import json
import os
from pathlib import Path
import statistics
import urllib.request
from urllib.parse import urlparse
import zipfile


def stamp(value):
    return datetime.fromisoformat(value.replace('Z', '+00:00')) if value else None


def seconds(start, end):
    return max(0, (stamp(end) - stamp(start)).total_seconds()) if start and end else None


def active_seconds(jobs):
    intervals = []
    for job in jobs:
        starts = [stamp(step['started_at']) for step in job.get('steps', []) if step.get('started_at')]
        if starts and job.get('completed_at'):
            intervals.append((min(starts), stamp(job['completed_at'])))
    total = 0.0
    end = None
    for start, stop in sorted(intervals):
        if stop <= start:
            continue
        total += max(0, (stop - max(start, end or start)).total_seconds())
        end = max(stop, end or stop)
    return total


def measure(run, jobs, observations):
    rows = []
    for job in jobs:
        starts = [s['started_at'] for s in job.get('steps', []) if s.get('started_at')]
        first = min(starts) if starts else None
        rows.append({'name': job['name'], 'conclusion': job.get('conclusion'),
                     'queue_seconds': seconds(job.get('started_at'), first),
                     'execution_seconds': seconds(first, job.get('completed_at')),
                     'steps': [{'name': step.get('name', '(unknown step)'), 'conclusion': step.get('conclusion'),
                                'seconds': seconds(step.get('started_at'), step.get('completed_at'))}
                               for step in job.get('steps', [])]})
    starts = [s['started_at'] for j in jobs for s in j.get('steps', []) if s.get('started_at')]
    ends = [j['completed_at'] for j in jobs if j.get('completed_at')]
    selection = next((o for o in observations if 'selection' in o), {})
    span = seconds(min(starts), max(ends)) if starts and ends else None
    active = active_seconds(jobs) if starts else None
    return {'metrics_schema': 2, 'run_id': run['id'], 'sha': run['head_sha'], 'event': run['event'],
            'conclusion': run.get('conclusion'), 'selection': selection.get('selection'),
            'queue_seconds': seconds(run['created_at'], min(starts)) if starts else None,
            'execution_seconds': active, 'execution_span_seconds': span,
            'idle_after_start_seconds': max(0, span - active) if span is not None else None,
            'time_to_result_seconds': seconds(run['created_at'], max(ends)) if ends else None,
            'runner_minutes': sum(r['execution_seconds'] or 0 for r in rows) / 60,
            'cancelled_jobs': sum(r['conclusion'] == 'cancelled' for r in rows),
            'cache_observations': [o for o in observations if 'caches' in o], 'jobs': rows}


def compare(before, after):
    # Event, coverage and result must match. Never present a smaller suite as faster execution.
    cohorts = {}
    for side, runs in [('before', before), ('after', after)]:
        for run in runs:
            if run.get('conclusion') != 'success' or run.get('selection') is None:
                continue
            key = json.dumps([run.get('metrics_schema'), run['event'], run['selection']], sort_keys=True)
            cohorts.setdefault(key, {'before': [], 'after': []})[side].append(run)
    result = []
    for key, group in cohorts.items():
        if min(len(group['before']), len(group['after'])) < 10:
            continue
        medians = {side: {metric: statistics.median(r[metric] for r in runs[-10:] if r[metric] is not None)
                          for metric in ('queue_seconds', 'execution_seconds', 'runner_minutes', 'time_to_result_seconds')}
                   for side, runs in group.items()}
        result.append({'cohort': json.loads(key), 'samples_per_side': 10, 'medians': medians})
    return result


def api(path, token, binary=False):
    # Artifact redirects carry no authorization header to a third-party origin.
    class SafeRedirect(urllib.request.HTTPRedirectHandler):
        def redirect_request(self, req, fp, code, msg, headers, newurl):
            redirect = super().redirect_request(req, fp, code, msg, headers, newurl)
            if urlparse(req.full_url).netloc != urlparse(newurl).netloc:
                redirect.remove_header('Authorization')
            return redirect
    request = urllib.request.Request('https://api.github.com/' + path,
                                     headers={'Authorization': 'Bearer ' + token, 'Accept': 'application/vnd.github+json'})
    with urllib.request.build_opener(SafeRedirect()).open(request, timeout=60) as response:
        body = response.read()
    return body if binary else json.loads(body)


def run_artifacts(prefix, token, read=api):
    page = 1
    while True:
        batch = read(prefix + f'/artifacts?per_page=100&page={page}', token)['artifacts']
        yield from batch
        if len(batch) < 100:
            return
        page += 1


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--repo', default=os.environ.get('GITHUB_REPOSITORY'))
    parser.add_argument('--run-id', required=False)
    parser.add_argument('--output', type=Path, default=Path('artifacts/ci-metrics.json'))
    parser.add_argument('--before', type=Path)
    parser.add_argument('--after', type=Path)
    args = parser.parse_args()
    if args.before and args.after:
        result = compare(json.loads(args.before.read_text()), json.loads(args.after.read_text()))
    else:
        token = os.environ['GH_TOKEN']
        prefix = f'repos/{args.repo}/actions/runs/{args.run_id}'
        run = api(prefix, token)
        jobs = []
        for page in range(1, 100):
            batch = api(prefix + f'/attempts/{run["run_attempt"]}/jobs?per_page=100&page={page}', token)['jobs']
            jobs += batch
            if len(batch) < 100:
                break
        observations = []
        for artifact in run_artifacts(prefix, token):
            if artifact['name'].startswith('ci-observation-') and not artifact['expired']:
                data = api(f'repos/{args.repo}/actions/artifacts/{artifact["id"]}/zip', token, True)
                with zipfile.ZipFile(io.BytesIO(data)) as archive:
                    for member in archive.infolist():
                        if member.filename.endswith('.json') and member.file_size <= 1024 * 1024:
                            observations.append(json.loads(archive.read(member)))
        result = measure(run, jobs, observations)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, indent=2) + '\n')
    summary = os.environ.get('GITHUB_STEP_SUMMARY')
    if summary:
        with open(summary, 'a') as out:
            out.write('## CI timing observations\n\n```json\n' + json.dumps(result, indent=2) + '\n```\n')
    print(json.dumps(result, indent=2))


if __name__ == '__main__':
    main()
