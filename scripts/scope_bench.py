#!/usr/bin/env python3
"""Alternate two builds continuing one save with the engine's scope timing on and
compare per-call time of the statistics step (telemetry.capture) and of a whole
simulation tick (simulation.tick).

usage: scope_bench.py BEFORE AFTER SAVE OUT.json [--ticks ABS] [--pairs N]
"""
import argparse, json, re, statistics, subprocess, tempfile
from pathlib import Path

SCOPES = ('telemetry.capture', 'simulation.tick')


def run(binary, save, ticks):
    with tempfile.TemporaryDirectory(prefix='glob2-scope-') as out:
        p = subprocess.run([binary, '--run-game', '--load-game', save, '--ticks', str(ticks), '--compute-threads', '1',
                            '--telemetry', 'team-timeline', '--output-dir', out],
                           stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, text=True, errors='replace')
        if p.returncode:
            raise SystemExit(f'{binary} failed')
    found = {}
    for line in p.stdout.splitlines():
        if not line.startswith('GLOB2_PERF_FINAL'):
            continue
        for scope in SCOPES:
            if f' scope={scope} ' in line:
                found[scope] = float(re.search(r' mean_ns=([0-9.]+)', line).group(1))
    return found


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('before'); ap.add_argument('after'); ap.add_argument('save'); ap.add_argument('out')
    ap.add_argument('--ticks', type=int, required=True)
    ap.add_argument('--pairs', type=int, default=5)
    a = ap.parse_args()
    rows = []
    for pair in range(a.pairs):
        for variant in (('before', 'after') if pair % 2 == 0 else ('after', 'before')):
            r = run(a.before if variant == 'before' else a.after, a.save, a.ticks)
            rows.append({'pair': pair, 'variant': variant, **r})
            print(json.dumps(rows[-1]), flush=True)
    summary = {'save': a.save, 'ticks': a.ticks, 'pairs': a.pairs, 'rows': rows}
    for scope in SCOPES:
        b = [r[scope] for r in rows if r['variant'] == 'before']
        f = [r[scope] for r in rows if r['variant'] == 'after']
        ratios = sorted(y / x for x, y in zip(b, f))
        summary[scope] = {'median_before_ns': statistics.median(b), 'median_after_ns': statistics.median(f),
                          'median_change_percent': 100 * (statistics.median(ratios) - 1),
                          'pair_change_percent_range': [100 * (ratios[0] - 1), 100 * (ratios[-1] - 1)]}
    Path(a.out).write_text(json.dumps(summary, indent=1))
    print(json.dumps({s: summary[s] for s in SCOPES}, indent=1))


if __name__ == '__main__':
    main()
