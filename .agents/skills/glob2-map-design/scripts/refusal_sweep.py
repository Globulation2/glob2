#!/usr/bin/env python3
"""How often does one control value refuse? Many seeds at a few values of one control.

  python3 refusal_sweep.py marchland lakes 0,10,20,25,30,35,40 --seeds 40
"""
import argparse, sys
from pathlib import Path
from concurrent.futures import ThreadPoolExecutor
sys.path.insert(0, str(Path(__file__).resolve().parents[4]))
from tools.map_generation_study import load_catalog, generator_definition, generate_map


def run(job):
    result = generate_map(job['binary'], job['method'], job['seed'], job['w'], job['h'],
                          job['teams'], {job['control']: job['value']}, job['timeout'])
    row = dict(job, ok=result['category'] == 'completed', category=result['category'],
               detail=result['detail'])
    if row['ok']:
        report = result['native']['map_report']
        row['water%'] = report['terrain']['water']['percent']
        row['sites4x4'] = report['space']['build_sites_4x4']
    return row


def refusal_rate(rows):
    evaluated = [r for r in rows if r['category'] in ('completed', 'refused')]
    return sum(r['category'] == 'refused' for r in evaluated) / len(evaluated) if evaluated else None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('gen'); ap.add_argument('control'); ap.add_argument('values')
    ap.add_argument('--seeds', type=int, default=40)
    ap.add_argument('--binary', default='build/src/glob2')
    ap.add_argument('--jobs', type=int, default=16)
    ap.add_argument('--w', type=int, default=256)
    ap.add_argument('--h', type=int, default=256)
    ap.add_argument('--teams', type=int, default=4)
    ap.add_argument('--timeout', type=float, default=120)
    a = ap.parse_args()
    definition = generator_definition(load_catalog(a.binary), a.gen)
    values = [int(v) for v in a.values.split(',')]
    jobs = [dict(binary=a.binary, gen=a.gen, method=definition['method'], timeout=a.timeout, control=a.control, value=v, seed=s,
                 w=a.w, h=a.h, teams=a.teams)
            for v in values for s in range(1, a.seeds + 1)]
    with ThreadPoolExecutor(a.jobs) as pool:
        rows = list(pool.map(run, jobs))
    print('Execution errors:', sum(r['category'] == 'execution_error' for r in rows))
    print(f'{a.gen} {a.control}, {a.seeds} seeds at {a.w}x{a.h} teams={a.teams}\n')
    print(f'{a.control:>8} | refused | rate  | exec errors | mean water% | mean sites4x4 | commonest refusal')
    for v in values:
        got = [r for r in rows if r['value'] == v]
        bad = [r for r in got if r['category'] == 'refused']
        good = [r for r in got if r['ok']]
        errors = sum(r['category'] == 'execution_error' for r in got)
        rate = refusal_rate(got)
        rate_text = f'{rate:5.0%}' if rate is not None else '  n/a'
        water = sum(r.get('water%', 0) for r in good) / len(good) if good else 0
        sites = sum(r.get('sites4x4', 0) for r in good) / len(good) if good else 0
        why = max({r['detail'][:60] for r in bad}, key=lambda d: sum(
            1 for r in bad if r['detail'][:60] == d), default='')
        print(f'{v:>8} | {len(bad):>7} | {rate_text} | {errors:>11} | {water:>11.1f} | '
              f'{sites:>13.0f} | {why}')


if __name__ == '__main__':
    main()
