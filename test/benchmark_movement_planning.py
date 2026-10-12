#!/usr/bin/env python3
"""Run paired Stage A movement diagnostics; an incomplete corpus cannot pass.

The manifest contains scenarios with id, workload, args, modes and qualified.
Use release binaries. Existing compute manifests can be adapted by adding these
fields; qualification requires retained evidence that each fixture exercises its
stated behavior. Outputs, commands, hashes and gate decisions are local evidence.
"""
import argparse
import hashlib
import json
import subprocess
from pathlib import Path

DELAYS = (1, 2, 4, 8)
WORKLOADS = {
    'large-colony', 'contention', 'team-imbalance', 'ground-terrain', 'air-hazards',
    'resources-markets', 'exploration-clearing', 'combat-pursuit', 'task-changes', 'ordinary-control',
}


def evaluate(records, scenarios):
    """Return per-delay decisions without inferring success from missing data."""
    missing = sorted(WORKLOADS - {s['workload'] for s in scenarios if s.get('qualified')})
    decisions = {}
    for delay in DELAYS:
        failures, incomplete, coverage = [], [], []
        if missing:
            incomplete.append('Missing qualified workloads: ' + ', '.join(missing))
        for scenario in scenarios:
            sid = scenario['id']
            reference = records.get((sid, 0))
            trial = records.get((sid, delay))
            if reference is None or trial is None:
                incomplete.append(f'{sid}: paired result missing')
                continue
            if reference.get('finalChecksum') != trial.get('finalChecksum'):
                failures.append(f'{sid}: shadow changed the reference final checksum')
            metrics = trial.get('movement_shadow', {})
            if metrics.get('delay') != delay or not metrics.get('workers_settled'):
                incomplete.append(f'{sid}: shadow output missing or workers unsettled')
                continue
            if metrics.get('maximum_batches', delay + 2) > delay + 1:
                failures.append(f'{sid}: publication horizon exceeded')
            for mode in scenario.get('modes', []):
                keys = [k for k in metrics if k.endswith(f'_{mode}_stable_opportunities')]
                stable = sum(metrics[k] for k in keys)
                valid = sum(metrics.get(k.removesuffix('stable_opportunities')+'valid', 0) for k in keys)
                if stable == 0:
                    incomplete.append(f'{sid}/{mode}: no stable opportunities')
                else:
                    rate = valid / stable
                    coverage.append(dict(scenario=sid, mode=mode, stable=stable, valid=valid, rate=rate))
                    if rate < .95:
                        failures.append(f'{sid}/{mode}: valid coverage {rate:.3%} < 95%')
            if not scenario.get('modes'):
                incomplete.append(f'{sid}: applicable movement modes unspecified')
            # The search counter is an inclusive subset of phase timers. Never
            # add it again, or add worker elapsed time to synchronous owner work.
            baseline = reference.get('movement_shadow', {})
            # Use all pre-visibility phases as a conservative upper bound: it
            # includes authoritative serial work that a live planner cannot remove.
            covered = sum(v for k, v in baseline.items() if k.endswith(tuple(f'reference_phase_{p}_ns' for p in range(4))))
            before = reference.get('ai_pipeline', {})
            after = trial.get('ai_pipeline', {})
            if 'snapshot_extraction_ns' in baseline:
                before = dict(extraction_ns=baseline['snapshot_extraction_ns'], preparation_ns=baseline.get('snapshot_preparation_ns', 0))
            if 'snapshot_extraction_ns' in metrics:
                after = dict(extraction_ns=metrics['snapshot_extraction_ns'], preparation_ns=metrics.get('snapshot_preparation_ns', 0))
            if not covered or 'extraction_ns' not in before or 'extraction_ns' not in after:
                incomplete.append(f'{sid}: owner/capture cost evidence unavailable')
            else:
                capture = max(0, after['extraction_ns'] + after.get('preparation_ns', 0)
                              - before['extraction_ns'] - before.get('preparation_ns', 0))
                overhead = metrics.get('admission_ns', 0) + capture
                if overhead >= covered:
                    failures.append(f'{sid}: owner admission/capture overhead consumes the entire covered reference cost')
        status = 'fail' if failures else 'inconclusive' if incomplete else 'pass'
        decisions[str(delay)] = dict(status=status, failures=failures, incomplete=incomplete, coverage=coverage)
    return dict(stage='A', delays=decisions,
                status='pass' if any(d['status'] == 'pass' for d in decisions.values())
                else 'fail' if all(d['status'] == 'fail' for d in decisions.values()) else 'inconclusive',
                live_implementation_allowed=any(d['status'] == 'pass' for d in decisions.values()))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('binary', type=Path)
    parser.add_argument('manifest', type=Path)
    parser.add_argument('output', type=Path)
    parser.add_argument('--participants', type=int, default=4)
    parser.add_argument('--evaluate-only', action='store_true')
    args = parser.parse_args()
    if args.participants < 1:
        parser.error('participants must be positive')
    scenarios = json.loads(args.manifest.read_text())['scenarios']
    if len({s['id'] for s in scenarios}) != len(scenarios):
        parser.error('scenario ids must be unique')
    args.output.mkdir(parents=True, exist_ok=True)
    records = {}
    for index, scenario in enumerate(scenarios):
        # Rotate the reference and four delays to avoid a fixed warm-cache order.
        order = [0, *DELAYS]
        shift = index % len(order)
        for delay in order[shift:] + order[:shift]:
            directory = args.output / scenario['id'] / str(delay)
            result = directory / 'result.json'
            if not args.evaluate_only:
                directory.mkdir(parents=True, exist_ok=False)
                command = [str(args.binary.resolve()), 'game', 'run', *scenario['args'],
                           '--compute-threads', str(args.participants), '--output-dir', str(directory.resolve())]
                command += ['--movement-profile'] if delay == 0 else ['--movement-shadow', str(delay)]
                (directory / 'command.json').write_text(json.dumps(command, indent=2))
                with (directory / 'run.log').open('w') as log:
                    subprocess.run(command, stdout=log, stderr=subprocess.STDOUT, check=True)
            if result.exists():
                records[(scenario['id'], delay)] = json.loads(result.read_text())
    report = evaluate(records, scenarios)
    report['binary_sha256'] = hashlib.sha256(args.binary.read_bytes()).hexdigest()
    report['manifest_sha256'] = hashlib.sha256(args.manifest.read_bytes()).hexdigest()
    (args.output / 'gate.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report, indent=2))
    raise SystemExit({'pass': 0, 'inconclusive': 2, 'fail': 3}[report['status']])


if __name__ == '__main__':
    main()
