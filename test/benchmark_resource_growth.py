"""Paired resource-growth engine measurements; timing is never a CI assertion.

Manifest: {"scenarios":[{"id":..., "args":[...], "fixture_sha256":{...}}]}.
Use fixed tick counts, baseline-format starting saves, and separate AI/control groups.
"""
import argparse
import json
import os
import platform
import random
import statistics
from pathlib import Path
from benchmark_parallel_compute import digest, execute


def interval(values):
    """Deterministic percentile bootstrap CI for a median paired ratio."""
    rng = random.Random(713)
    medians = sorted(statistics.median(rng.choices(values, k=len(values))) for _ in range(2000))
    return [medians[49], medians[1949]]


def summarize(rows):
    output = {}
    for scenario in sorted({r['scenario'] for r in rows}):
        samples = [r for r in rows if r['scenario'] == scenario and r['repeat'] >= 0]
        variants = {}
        for name in sorted({r['variant'] for r in samples}):
            selected = [r for r in samples if r['variant'] == name]
            entry = {k: statistics.median(r[k] for r in selected) for k in ('wall_s', 'cpu_s', 'peak_rss_bytes')}
            entry['run_s'] = statistics.median(r['result']['run_ns']/1e9 for r in selected)
            entry['tick_percentiles_ns'] = {k: statistics.median(r['result'][k] for r in selected) for k in ('tick_p50_ns', 'tick_p95_ns', 'tick_p99_ns') if k in selected[0]['result']}
            entry['growth'] = {k: statistics.median(r['result'][k] for r in selected)
                               for k in selected[0]['result'] if k.startswith('growth_')}
            thread_count = name.split('t')[-1].split('-')[0] if name.startswith('d') else name.rsplit('t', 1)[-1]
            for control in [f'legacy-t{thread_count}', name.split('t')[0] + 't1']:
                reference = {r['repeat']: r for r in samples if r['variant'] == control}
                if not reference:
                    continue
                pairs = [r for r in selected if r['repeat'] in reference]
                ratios = [r['result']['run_ns']/reference[r['repeat']]['result']['run_ns'] for r in pairs]
                entry[control] = {'run_time_ratio': statistics.median(ratios),
                                  'ratio_95pct_ci': interval(ratios),
                                  'run_delta_ms': statistics.median((r['result']['run_ns']-reference[r['repeat']]['result']['run_ns'])/1e6 for r in pairs),
                                  'cpu_ratio': statistics.median(r['cpu_s']/reference[r['repeat']]['cpu_s'] for r in pairs)}
            variants[name] = entry
        output[scenario] = variants
    return output


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('binary', type=Path)
    p.add_argument('manifest', type=Path)
    p.add_argument('--baseline', type=Path)
    p.add_argument('--output', type=Path, required=True)
    p.add_argument('--delays', type=int, nargs='+', default=[1, 3, 8])
    p.add_argument('--threads', type=int, nargs='+', default=[1, 2, 4, 8])
    p.add_argument('--repeats', type=int, default=10)
    p.add_argument('--verify', action='store_true')
    a = p.parse_args()
    if a.repeats < 1 or any(d < 1 or d > 16 for d in a.delays) or any(t < 1 or t > 64 for t in a.threads):
        p.error('invalid repetitions, delay or thread count')
    manifest = json.loads(a.manifest.read_text())
    variants = []
    for d in a.delays:
        for threads in a.threads:
            variants.append((f'd{d}t{threads}', a.binary.resolve(),
                             ['--resource-growth-delay', str(d), '--compute-threads', str(threads)]))
    if a.baseline and not a.verify:
        variants[0:0] = [(f'legacy-t{t}', a.baseline.resolve(), ['--compute-threads', str(t)]) for t in a.threads]
    a.output.mkdir(parents=True, exist_ok=False)
    for s in manifest['scenarios']:
        for name, expected in s.get('fixture_sha256', {}).items():
            if digest(name) != expected:
                raise ValueError(f'fixture changed: {name}')
    metadata = {'platform': platform.platform(), 'cpu_count': os.cpu_count(),
                'affinity': sorted(os.sched_getaffinity(0)) if hasattr(os, 'sched_getaffinity') else None,
                'manifest': manifest,
                'binaries': {str(exe): digest(exe) for _, exe, _ in variants},
                'repeats': a.repeats, 'delays': a.delays, 'threads': a.threads,
                'note': 'Old/new games may diverge; only same-delay candidate worker counts must match. CPU covers all child threads. Bootstrap intervals describe this host/run set.'}
    (a.output/'metadata.json').write_text(json.dumps(metadata, indent=2))
    rows = []
    with (a.output/'measurements.jsonl').open('w') as log:
        for s in manifest['scenarios']:
            references = {}
            for repeat in ([0] if a.verify else range(-1, a.repeats)):
                order = variants[repeat % len(variants):] + variants[:repeat % len(variants)]
                if repeat % 2:
                    order = order[::-1]
                for name, exe, extra in order:
                    args = s['args'] + extra
                    if a.verify:
                        args += ['--telemetry', 'checksums']
                    else:
                        args += ['--benchmark-warmup', '0']
                    load_before = Path('/proc/loadavg').read_text().strip() if Path('/proc/loadavg').exists() else None
                    row = dict(scenario=s['id'], repeat=repeat, variant=name, load_before=load_before,
                               **execute(exe, args, a.output/s['id']/str(repeat)/name))
                    if '--ticks' in s['args'] and row['result']['ticks'] != int(s['args'][s['args'].index('--ticks') + 1]):
                        raise RuntimeError(f"{s['id']} {name}: ended before the fixed tick limit")
                    if a.verify:
                        delay = name.split('t')[0]
                        result = row['result']
                        signature = (digest(a.output/s['id']/str(repeat)/name/'world.checksums'),
                                     digest(a.output/s['id']/str(repeat)/name/'game.replay.checksums'),
                                     {k: result[k] for k in ('ticks', 'finalChecksum', 'growth_submitted', 'growth_published', 'growth_accepted', 'growth_rejected')})
                        if delay in references and signature != references[delay]:
                            raise AssertionError((s['id'], name, 'determinism mismatch'))
                        references[delay] = signature
                    rows.append(row)
                    log.write(json.dumps(row)+'\n'); log.flush()
                    print(s['id'], repeat, name, f"wall={row['wall_s']:.3f}s cpu={row['cpu_s']:.3f}s", flush=True)
    for path, expected in metadata['binaries'].items():
        if digest(path) != expected:
            raise RuntimeError(f'binary changed during measurements: {path}')
    for s in manifest['scenarios']:
        for path, expected in s.get('fixture_sha256', {}).items():
            if digest(path) != expected:
                raise RuntimeError(f'fixture changed during measurements: {path}')
    metadata['completed'] = True
    (a.output/'metadata.json').write_text(json.dumps(metadata, indent=2))
    (a.output/'summary.json').write_text(json.dumps(summarize(rows), indent=2))


if __name__ == '__main__':
    main()
