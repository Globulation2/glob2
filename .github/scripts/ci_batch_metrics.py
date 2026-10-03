"""One measurement job per batch, rather than a new job for every cancellation."""
import io
import json
import zipfile
import os
from pathlib import Path
import subprocess
from ci_run_metrics import api, feedback


def previous_batch(repo, token):
    """Carry inert JSON forward so hourly batches measure each attempt once."""
    runs = api(f'repos/{repo}/actions/workflows/ci-metrics.yml/runs?status=success&branch=master&per_page=5', token)['workflow_runs']
    for run in runs:
        artifacts = api(f'repos/{repo}/actions/runs/{run["id"]}/artifacts?per_page=100', token)['artifacts']
        for artifact in artifacts:
            if artifact['name'] != 'ci-metrics-batch' or artifact['expired']:
                continue
            raw = api(f'repos/{repo}/actions/artifacts/{artifact["id"]}/zip', token, True)
            with zipfile.ZipFile(io.BytesIO(raw)) as archive:
                for member in archive.infolist():
                    if member.filename == 'state.json' and member.file_size <= 16 * 1024 * 1024:
                        state = json.loads(archive.read(member))
                        if state.get('schema') == 1:
                            return state
    return {'schema': 1, 'measured': [], 'runs': []}


def main():
    repo, token = os.environ['GITHUB_REPOSITORY'], os.environ['GH_TOKEN']
    requested = os.environ.get('REQUESTED_RUN')
    if requested:
        if not requested.isdigit():
            raise ValueError('Run ID must be numeric')
        runs = [api(f'repos/{repo}/actions/runs/{requested}', token)]
    else:
        # Bound hourly work. Ten matching samples remain required for comparisons.
        runs = api(f'repos/{repo}/actions/workflows/build.yml/runs?status=completed&per_page=100', token)['workflow_runs']
    state = previous_batch(repo, token)
    measured = set(state['measured'])
    results = {str(row['run_id']): row for row in state['runs']}
    for run in runs:
        identity = f'{run["id"]}:{run.get("run_attempt", 1)}'
        if not requested and identity in measured:
            continue
        if run.get('conclusion') not in ('success', 'failure'):
            continue
        path = Path(f'artifacts/ci-metrics/{run["id"]}.json')
        subprocess.run(['python3', '.github/scripts/ci_run_metrics.py', '--run-id', str(run['id']), '--output', str(path)], check=True)
        observed = json.loads(path.read_text())
        measured.add(identity)
        results.pop(str(run['id']), None)
        if observed['draft'] or not any((observed.get('selection') or {}).get(flag) for flag in ('native','browser','android','map_generators')):
            path.unlink()
        else:
            results[str(run['id'])] = {key: value for key, value in observed.items() if key not in ('jobs', 'cache_observations')}
    root = Path('artifacts/ci-metrics')
    root.mkdir(parents=True, exist_ok=True)
    # Bound archive size while retaining enough samples across coverage cohorts.
    results = sorted(results.values(), key=lambda row: int(row['run_id']))[-1000:]
    measured = sorted(measured, key=lambda key: int(key.split(':')[0]))[-2000:]
    (root / 'runs.json').write_text(json.dumps(results, indent=2) + '\n')
    (root / 'feedback.json').write_text(json.dumps(feedback(results), indent=2) + '\n')
    (root / 'state.json').write_text(json.dumps(dict(schema=1, measured=measured, runs=results)) + '\n')


if __name__ == '__main__':
    main()
