#!/usr/bin/env python3
"""Paired ABBA CPU comparison of two glob2 builds continuing the same save.

Uses the engine's own execution CPU measurement after a warmup (result.json),
single-threaded, so loading and saving are excluded.

usage: bench.py BEFORE AFTER SAVE OUT.json [--ticks N] [--warmup N] [--pairs N]
                [--telemetry NAME ...]
"""
import argparse, json, random, statistics, subprocess, tempfile
from pathlib import Path


def run(binary, save, ticks, warmup, telemetry):
    with tempfile.TemporaryDirectory(prefix='glob2-bench-') as out:
        cmd = [binary, '--run-game', '--load-game', save, '--ticks', str(ticks),
               '--benchmark-warmup', str(warmup), '--compute-threads', '1', '--output-dir', out]
        for t in telemetry:
            cmd += ['--telemetry', t]
        p = subprocess.run(cmd, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE, text=True)
        if p.returncode:
            raise SystemExit(f'{cmd} failed:\n{p.stderr[-2000:]}')
        return json.loads((Path(out) / 'result.json').read_text())


def execution_ns(result):
    # Find the execution CPU figure wherever the result schema keeps it.
    stack = [result]
    while stack:
        node = stack.pop()
        if isinstance(node, dict):
            for k, v in node.items():
                if k == 'benchmark_run_cpu_ns':
                    return v
                stack.append(v)
    raise SystemExit('no execution CPU figure in result.json: ' + json.dumps(result)[:2000])


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('before'); ap.add_argument('after'); ap.add_argument('save'); ap.add_argument('out')
    ap.add_argument('--ticks', type=int, required=True)
    ap.add_argument('--warmup', type=int, default=256)
    ap.add_argument('--pairs', type=int, default=10)
    ap.add_argument('--telemetry', nargs='*', default=[])
    a = ap.parse_args()
    rows = []
    for pair in range(a.pairs):
        order = ('before', 'after', 'after', 'before') if pair % 2 == 0 else ('after', 'before', 'before', 'after')
        for variant in order:
            ns = execution_ns(run(a.before if variant == 'before' else a.after, a.save, a.ticks, a.warmup, a.telemetry))
            rows.append({'pair': pair, 'variant': variant, 'execution_cpu_ns': ns})
            print(json.dumps(rows[-1]), flush=True)
    def per_pair(v):
        return [statistics.mean(r['execution_cpu_ns'] for r in rows if r['pair'] == p and r['variant'] == v)
                for p in range(a.pairs)]
    b, f = per_pair('before'), per_pair('after')
    ratios = [y / x for x, y in zip(b, f)]
    rng = random.Random(1)
    boot = sorted(statistics.median(rng.choice(ratios) for _ in ratios) for _ in range(5000))
    summary = {'save': a.save, 'ticks': a.ticks, 'warmup': a.warmup, 'pairs': a.pairs, 'telemetry': a.telemetry,
               'median_before_ms': statistics.median(b) / 1e6, 'median_after_ms': statistics.median(f) / 1e6,
               'median_change_percent': 100 * (statistics.median(ratios) - 1),
               'ci95_change_percent': [100 * (boot[125] - 1), 100 * (boot[4875] - 1)], 'rows': rows}
    Path(a.out).write_text(json.dumps(summary, indent=1))
    print(json.dumps({k: v for k, v in summary.items() if k != 'rows'}))


if __name__ == '__main__':
    main()
