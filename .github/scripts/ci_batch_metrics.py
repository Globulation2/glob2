"""One measurement job per batch, rather than a new job for every cancellation."""
import json
import os
from pathlib import Path
import subprocess
from ci_run_metrics import api


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
    results = []
    for run in runs:
        if run.get('conclusion') not in ('success', 'failure'):
            continue
        path = Path(f'artifacts/ci-metrics/{run["id"]}.json')
        subprocess.run(['python3', '.github/scripts/ci_run_metrics.py', '--run-id', str(run['id']), '--output', str(path)], check=True)
        observed = json.loads(path.read_text())
        if observed['draft'] or not any((observed.get('selection') or {}).get(flag) for flag in ('native','browser','android','map_generators')):
            path.unlink()
        else:
            results.append(observed)
    root = Path('artifacts/ci-metrics')
    root.mkdir(parents=True, exist_ok=True)
    (root / 'runs.json').write_text(json.dumps(results, indent=2) + '\n')


if __name__ == '__main__':
    main()
