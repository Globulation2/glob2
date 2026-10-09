"""Measure fixed-delay gradient schedules independently of worker count.

Zero workers is the serial reference for each delay. Timing runs omit checksums;
--verify compares exact traces separately. Input manifests match
benchmark_parallel_compute.py. Retain the output directory as review evidence.
"""
import argparse
import json
import math
import platform
import statistics
from pathlib import Path
from benchmark_parallel_compute import execute, digest


def summarize(rows, scenarios):
    output = {}
    for scenario in scenarios:
        measured = [r for r in rows if r['scenario'] == scenario['id'] and r['repeat'] >= 0]
        variants = {}
        for label in sorted({r['variant'] for r in measured}):
            subset = [r for r in measured if r['variant'] == label]
            entry = {key: statistics.median(r[key] for r in subset) for key in ('wall_s', 'cpu_s', 'peak_rss_bytes')}
            entry['wall_min_s'] = min(r['wall_s'] for r in subset)
            entry['wall_max_s'] = max(r['wall_s'] for r in subset)
            for key in ('run_ns', 'gradient_wait_ns', 'gradient_active_elapsed_ns', 'gradient_jobs', 'gradient_discarded', 'gradient_max_pending', 'gradient_workers'):
                if all(key in r['result'] for r in subset):
                    entry[key] = statistics.median(r['result'][key] for r in subset)
            entry['ticks'] = sorted({r['result']['ticks'] for r in subset})
            entry['termination'] = sorted({r['result']['termination'] for r in subset})
            variants[label] = entry
        for label, entry in variants.items():
            if label == 'legacy':
                continue
            control = variants.get(label.split('-w')[0] + '-w0')
            if control:
                entry['wall_speedup'] = control['wall_s'] / entry['wall_s']
                entry['cpu_ratio'] = entry['cpu_s'] / control['cpu_s']
                entry['run_speedup'] = control['run_ns'] / entry['run_ns']
                entry['same_ticks'] = control['ticks'] == entry['ticks']
            if 'legacy' in variants:
                entry['legacy_wall_speedup'] = variants['legacy']['wall_s'] / entry['wall_s']
                entry['legacy_cpu_ratio'] = entry['cpu_s'] / variants['legacy']['cpu_s']
        output[scenario['id']] = variants
    aggregate = {}
    labels = set.intersection(*(set(v) for v in output.values())) if output else set()
    for label in sorted(labels - {'legacy'}):
        grouped = {}
        for s in scenarios:
            if not s.get('control'):
                grouped.setdefault(s.get('group', s['id']), []).append(output[s['id']][label])
        if not grouped:
            continue
        if not all('wall_speedup' in e for group in grouped.values() for e in group):
            continue
        entries = [e for group in grouped.values() for e in group]
        agg = {}
        for key in ('wall_speedup', 'cpu_ratio', 'legacy_wall_speedup', 'legacy_cpu_ratio'):
            if all(key in e for e in entries):
                agg[key] = math.exp(statistics.mean(statistics.mean(math.log(e[key]) for e in group) for group in grouped.values()))
        agg['worst_cpu_ratio'] = max(e['cpu_ratio'] for e in entries)
        controls = [output[s['id']][label] for s in scenarios if s.get('control')]
        agg['worst_control_speedup'] = min((e['wall_speedup'] for e in controls), default=None)
        agg['meets_threading_target'] = (agg['wall_speedup'] >= 1.5 and agg['worst_cpu_ratio'] <= 1.1
            and all(e['same_ticks'] for e in entries) and all(e['wall_speedup'] >= 1/1.05 for e in controls))
        aggregate[label] = agg
    return {'scenarios': output, 'aggregate': aggregate}


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('binary', type=Path)
    p.add_argument('manifest', type=Path)
    p.add_argument('--output', type=Path, required=True)
    p.add_argument('--baseline', type=Path)
    p.add_argument('--workers', type=int, nargs='+', default=[0, 1, 2, 4, 8])
    p.add_argument('--delays', type=int, nargs='+', default=[1, 3, 8])
    p.add_argument('--repeats', type=int, default=3)
    p.add_argument('--verify', action='store_true')
    a = p.parse_args()
    if 0 not in a.workers or any(w < 0 or w > 16 for w in a.workers) or any(d < 1 or d > 16 for d in a.delays) or a.repeats < 1:
        p.error('include serial control 0, workers 0..16, delays 1..16, positive repeats')
    binary, output = a.binary.resolve(), a.output.resolve()
    manifest = json.loads(a.manifest.read_text())
    for s in manifest['scenarios']:
        for path, expected in s.get('fixture_sha256', {}).items():
            assert digest(path) == expected, path
        if '--save' in s['args'] or '--replay' in s['args']:
            p.error('pipeline scenarios must omit save/replay exports')
    output.mkdir(parents=True, exist_ok=False)
    variants = [(f'd{d}-w{w}', binary, ['--compute-threads', str(w + 1), '--gradient-delay', str(d)]) for d in a.delays for w in a.workers]
    if a.baseline and not a.verify:
        variants.insert(0, ('legacy', a.baseline.resolve(), []))
    metadata = {'platform': platform.platform(), 'manifest': manifest, 'binaries': {str(exe): digest(exe) for _, exe, _ in variants},
                'workers': a.workers, 'delays': a.delays, 'repeats': a.repeats, 'verification': a.verify,
                'note': 'CPU is child user+system across all threads; delay changes behavior. Zero workers per delay is the threading control. Host contention may affect wall times.'}
    (output/'metadata.json').write_text(json.dumps(metadata, indent=2))
    rows = []
    with (output/'measurements.jsonl').open('w') as log:
        for scenario in manifest['scenarios']:
            references = {}
            for repeat in ([0] if a.verify else range(-1, a.repeats)):
                shift = (repeat+1) % len(variants)
                ordered = variants[shift:]+variants[:shift]
                if repeat % 2:
                    ordered = list(reversed(ordered))
                for label, exe, extra in ordered:
                    args = scenario['args'] + extra + (['--telemetry', 'checksums'] if a.verify else [])
                    dest = output/scenario['id']/str(repeat)/label
                    row = dict(scenario=scenario['id'], variant=label, repeat=repeat, **execute(exe, args, dest))
                    if not a.verify and label != 'legacy' and row['result']['gradient_workers'] != int(label.split('-w')[1]):
                        raise RuntimeError(f'worker creation fell back to serial: {label}; timing is not representative')
                    if a.verify:
                        # Same delay must match even if the worker finished much earlier/later.
                        signature = {'trace': digest(dest/'game.replay.checksums'),
                                     'result': {k: row['result'][k] for k in ('ticks', 'termination', 'teams', 'gradient_jobs', 'gradient_published', 'gradient_discarded',
                                                'building_gradient_jobs', 'building_gradient_published', 'building_gradient_discarded',
                                                'building_gradient_synchronous', 'building_gradient_max_pending') if k in row['result']}}
                        delay = label.split('-w')[0]
                        if delay in references:
                            assert signature == references[delay], (scenario['id'], label, 'determinism mismatch')
                        references[delay] = signature
                    rows.append(row); log.write(json.dumps(row)+'\n'); log.flush()
                    print(f"{scenario['id']} {repeat} {label}: wall={row['wall_s']:.3f}s cpu={row['cpu_s']:.3f}s", flush=True)
    (output/'summary.json').write_text(json.dumps(summarize(rows, manifest['scenarios']), indent=2))
    print('PASS exact same-schedule traces and outcomes' if a.verify else 'Completed paired pipeline measurements', flush=True)


if __name__ == '__main__':
    main()
