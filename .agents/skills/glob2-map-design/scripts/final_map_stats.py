#!/usr/bin/env python3
"""Final-map statistics at default settings: what a player actually gets.

  python3 final_map_stats.py --binary build/src/glob2 --generators even-ground,marchland,contested-commons
"""
import argparse, statistics as st, sys
from pathlib import Path
from concurrent.futures import ThreadPoolExecutor
sys.path.insert(0, str(Path(__file__).resolve().parents[4]))
from tools.map_generation_study import load_catalog, generator_definition, generate_map


def one(job):
    result = generate_map(job['binary'], job['method'], job['seed'], job['w'], job['h'],
                          job['teams'], timeout=job['timeout'])
    row = None
    if result['category'] == 'completed':
        d = result['native']['map_report']
        t, res = d['terrain'], d['resources']['types']
        q = (d.get('generation') or {}).get('selection_quality') or {}
        row = {
            'grass': t['grass']['percent'], 'sand': t['sand']['percent'],
            'water': t['water']['percent'],
            'wheat': res['wheat']['coverage']['tiles'], 'wood': res['wood']['coverage']['tiles'],
            'stone': res['stone']['coverage']['tiles'],
            'fruit': sum(res[k]['coverage']['tiles'] for k in ('cherry', 'orange', 'prune')),
            'sites4x4': d['space']['build_sites_4x4'],
            'fertility': d['fertility']['all_tiles']['mean'],
            'land_regions': d['space']['land_regions']['components'],
            'fairness': q.get('score'),
            'worst_fit': q.get('worst_fitness'),
        }
    return job['gen'], row, result['category'], result['detail']


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--binary', default='build/src/glob2')
    ap.add_argument('--generators', required=True)
    ap.add_argument('--seeds', type=int, default=16)
    ap.add_argument('--w', type=int, default=256)
    ap.add_argument('--h', type=int, default=256)
    ap.add_argument('--teams', type=int, default=4)
    ap.add_argument('--jobs', type=int, default=16)
    ap.add_argument('--timeout', type=float, default=120)
    a = ap.parse_args()
    gens = a.generators.split(',')
    catalog = load_catalog(a.binary)
    methods = {g: generator_definition(catalog, g)['method'] for g in gens}
    jobs = [dict(binary=a.binary, gen=g, method=methods[g], timeout=a.timeout, seed=s, w=a.w, h=a.h, teams=a.teams)
            for g in gens for s in range(1, a.seeds + 1)]
    with ThreadPoolExecutor(a.jobs) as pool:
        got = list(pool.map(one, jobs))
    print('Execution errors:', sum(category == 'execution_error' for _, _, category, _ in got))
    cols = ['grass', 'sand', 'water', 'wheat', 'wood', 'stone', 'fruit', 'sites4x4',
            'fertility', 'land_regions', 'fairness', 'worst_fit']
    print(f'Default settings, {a.w}x{a.h}, {a.teams} colonies, mean of {a.seeds} seeds '
          f'(failed seeds excluded)\n')
    print('| generator | ok | ' + ' | '.join(cols) + ' |')
    print('|---|---|' + '---|' * len(cols))
    for g in gens:
        rows = [r for gen, r, category, detail in got if gen == g and r]
        n = sum(1 for gen, r, category, detail in got if gen == g)
        if not rows:
            print(f'| {g} | 0/{n} |' + ' |' * len(cols))
            continue
        cells = []
        for c in cols:
            vals = [r[c] for r in rows if r[c] is not None]
            cells.append(f'{st.mean(vals):.3f}' if c in ('fairness', 'worst_fit') and vals else
                         (f'{st.mean(vals):.1f}' if vals else '-'))
        print(f'| {g} | {len(rows)}/{n} | ' + ' | '.join(cells) + ' |')


if __name__ == '__main__':
    main()
