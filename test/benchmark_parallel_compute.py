"""Retained paired wall/CPU benchmarks for the opt-in compute experiments.

A manifest contains scenarios with id, args (headless arguments without output),
optional group, and fixture_sha256 (absolute paths to input hashes). Run one
process at a time; process CPU includes all threads. Correctness runs are separate
from timing. No performance thresholds belong in CI.
"""
import argparse
import hashlib
import json
import math
import platform
import os
import statistics
import subprocess
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def execute(binary, args, output):
    output.mkdir(parents=True, exist_ok=False)
    command = [str(binary), '--run-game', *args, '--output-dir', str(output)]
    started = time.perf_counter()
    with (output / 'engine.log').open('w') as log:
        process = subprocess.Popen(command, cwd=ROOT, stdout=log, stderr=subprocess.STDOUT)
        _, status, usage = os.wait4(process.pid, 0)
        process.returncode = os.waitstatus_to_exitcode(status)
    wall = time.perf_counter() - started
    if process.returncode:
        raise RuntimeError(f"exit {process.returncode}: {output / 'engine.log'}")
    result = json.loads((output / 'result.json').read_text())
    return dict(command=command, wall_s=wall, cpu_s=usage.ru_utime + usage.ru_stime,
                user_s=usage.ru_utime, system_s=usage.ru_stime,
                peak_rss_bytes=usage.ru_maxrss * (1 if platform.system() == "Darwin" else 1024), result=result)


def summarize(rows):
    summaries = {}
    for scenario in sorted({row['scenario'] for row in rows}):
        selected = [row for row in rows if row['scenario'] == scenario and row['repeat'] >= 0]
        variants = {}
        for variant in sorted({row['variant'] for row in selected}):
            samples = [row for row in selected if row['variant'] == variant]
            variants[variant] = {key: statistics.median(row[key] for row in samples) for key in ('wall_s', 'cpu_s', 'peak_rss_bytes')}
            if all('run_ns' in row['result'] for row in samples):
                variants[variant]['run_s'] = statistics.median(row['result']['run_ns'] / 1e9 for row in samples)
            variants[variant]['ticks'] = sorted({row['result']['ticks'] for row in samples})
            variants[variant]['termination'] = sorted({row['result']['termination'] for row in samples})
        baseline = variants.get('baseline')
        if baseline:
            for value in variants.values():
                value['wall_speedup'] = baseline['wall_s'] / value['wall_s']
                value['cpu_ratio'] = value['cpu_s'] / baseline['cpu_s']
        for name, value in variants.items():
            if name != 'baseline':
                serial = variants.get(name.rsplit('-', 1)[0] + '-1')
                if serial:
                    value['speedup_over_prototype_serial'] = serial['wall_s'] / value['wall_s']
                    if 'run_s' in serial and 'run_s' in value:
                        value['runtime_speedup_over_prototype_serial'] = serial['run_s'] / value['run_s']
        summaries[scenario] = variants
    return summaries


def aggregate_summary(summary, scenarios):
    groups = {s['id']: s.get('group', s['id']) for s in scenarios}
    controls = {s['id'] for s in scenarios if s.get('control', False)}
    aggregate = {}
    variants = set.intersection(*(set(values) for values in summary.values())) if summary else set()
    for variant in sorted(variants):
        heavy = [values[variant] for name, values in summary.items() if name not in controls]
        small = [values[variant] for name, values in summary.items() if name in controls]
        if not heavy:
            continue
        result = {}
        for key in ('wall_speedup', 'cpu_ratio'):
            by_group = {}
            for scenario, values in summary.items():
                if scenario not in controls:
                    by_group.setdefault(groups[scenario], []).append(math.log(values[variant][key]))
            result[key] = math.exp(statistics.mean(statistics.mean(v) for v in by_group.values()))
        result['worst_cpu_ratio'] = max(v['cpu_ratio'] for v in heavy)
        result['worst_control_speedup'] = min((v['wall_speedup'] for v in small), default=None)
        result['meets_target_on_measured_suite'] = result['wall_speedup'] >= 1.5 and all(v['cpu_ratio'] <= 1.10 for v in heavy) and all(v['wall_speedup'] >= 1 / 1.05 for v in small)
        aggregate[variant] = result
    return aggregate


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('before', type=Path)
    parser.add_argument('after', type=Path)
    parser.add_argument('manifest', type=Path)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--repeats', type=int, default=5)
    parser.add_argument('--threads', type=int, nargs='+', default=[1, 2, 4, 8])
    parser.add_argument('--experiments', nargs='+', choices=['none', 'areas', 'initialize', 'hiring', 'all'], default=['areas', 'initialize', 'hiring', 'all'])
    parser.add_argument('--verify', action='store_true', help='Compare per-tick traces, replay orders and final saves, without timing repeats')
    args = parser.parse_args()
    before, after, output = args.before.resolve(), args.after.resolve(), args.output.resolve()
    if args.repeats < 1 or any(n < 1 or n > 64 for n in args.threads):
        parser.error('positive repeats and threads in 1..64 required')
    manifest = json.loads(args.manifest.read_text())
    for scenario in manifest['scenarios']:
        for path, expected in scenario.get('fixture_sha256', {}).items():
            if digest(path) != expected:
                raise ValueError(f'fixture changed: {path}')
    output.mkdir(parents=True, exist_ok=False)
    metadata = dict(platform=platform.platform(), binaries={str(p): digest(p) for p in (before, after)}, manifest=manifest,
                    arguments=vars(args) | {'before': str(before), 'after': str(after), 'output': str(output), 'manifest': str(args.manifest.resolve())},
                    memory_note='wait4 peak resident bytes, measured separately for each child')
    (output / 'metadata.json').write_text(json.dumps(metadata, indent=2))
    variants = [('baseline', before, [])]
    for experiment in args.experiments:
        for threads in args.threads:
            variants.append((f'{experiment}-{threads}', after, ['--compute-threads', str(threads), '--compute-experiments', experiment]))
    rows = []
    with (output / 'measurements.jsonl').open('w') as stream:
        for scenario in manifest['scenarios']:
            reference = None
            for repeat in ([0] if args.verify else range(-1, args.repeats)):
                # Rotate then reverse on alternate rounds to distribute thermal drift.
                shift = (repeat + 1) % len(variants)
                order = variants[shift:] + variants[:shift]
                if repeat % 2: order = list(reversed(order))
                for variant, binary, extra in order:
                    run_args = list(scenario['args']) + extra
                    if args.verify:
                        run_args += ['--telemetry', 'checksums', '--replay', 'true', '--save', 'final']
                    directory = output / scenario['id'] / str(repeat) / variant
                    row = dict(scenario=scenario['id'], variant=variant, repeat=repeat,
                               **execute(binary, run_args, directory))
                    rows.append(row)
                    stream.write(json.dumps(row) + '\n'); stream.flush()
                    if args.verify:
                        hashes = {name: digest(directory / name) for name in ('game.replay.checksums', 'game.replay', 'final.game')}
                        if reference is None: reference = hashes
                        if hashes != reference:
                            raise RuntimeError(f"correctness mismatch: {scenario['id']} {variant}: {hashes} != {reference}")
                    print(f"{scenario['id']} {repeat} {variant}: wall={row['wall_s']:.3f}s cpu={row['cpu_s']:.3f}s ticks={row['result']['ticks']}", flush=True)
    summary = summarize(rows)
    aggregate = aggregate_summary(summary, manifest['scenarios'])
    (output / 'summary.json').write_text(json.dumps(dict(scenarios=summary, aggregate=aggregate), indent=2))


if __name__ == '__main__':
    main()
