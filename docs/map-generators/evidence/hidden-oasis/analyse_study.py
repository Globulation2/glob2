#!/usr/bin/env python3
"""Tables for the Hidden Oasis control study (ablation.jsonl, random.jsonl)."""
import json, statistics as st, collections, sys
rows = [json.loads(l) for l in open('ablation.jsonl')]
def tel(r, k):
    v = r.get('tel', {}).get(k)
    return None if not v else (sum(v) / len(v) if isinstance(v[0], (int, float)) else v[0])
METRICS = {
 'water%': lambda r: r['terrain']['water'] + r['terrain']['sand_water_border'],
 'sand%': lambda r: r['terrain']['sand'] + r['terrain']['grass_sand_border'],
 'wheat': lambda r: r['res'].get('wheat', 0), 'wood': lambda r: r['res'].get('wood', 0),
 'stone': lambda r: r['res'].get('stone', 0), 'algae': lambda r: r['res'].get('algae', 0),
 'fruit': lambda r: sum(r['res'].get(k, 0) for k in ('cherry', 'orange', 'prune')),
 'sites4x4': lambda r: r['sites4x4'], 'fertility': lambda r: r['fert_mean'],
 'pondR': lambda r: tel(r, 'pond.radius'), 'basinRoom': lambda r: tel(r, 'basin.room-3x3'),
 'rock': lambda r: tel(r, 'rock.tiles'), 'mesas': lambda r: tel(r, 'buttes.placed'),
 'springs': lambda r: tel(r, 'springs.placed'), 'pondsDug': lambda r: sum(r['tel'].get('ponds.dug', [0])),
 'spacing': lambda r: tel(r, 'sites.spacing'), 'mouthWalk': lambda r: tel(r, 'mouth.walk'),
 'basinWalk': lambda r: tel(r, 'basin.walk'), 'scrub': lambda r: tel(r, 'scrub.planted'),
}
SHOW = {'pond-size': ['pondR', 'water%', 'basinRoom', 'rock', 'algae', 'basinWalk'],
        'pond-algae': ['algae'], 'starting-towers': ['sites4x4', 'stone'], 'starting-school': ['sites4x4'],
        'desert': ['sand%', 'sites4x4', 'fertility', 'wheat', 'wood', 'scrub', 'pondsDug', 'spacing'],
        'buttes': ['mesas', 'stone', 'sites4x4'], 'springs': ['springs', 'water%', 'fertility', 'wheat', 'pondsDug', 'spacing'],
        'wheat-amount': ['wheat', 'wood', 'sites4x4'], 'wood-amount': ['wood', 'wheat', 'scrub', 'sites4x4'],
        'stone-amount': ['stone', 'sites4x4'], 'fruit-amount': ['fruit']}
DEFAULT = {'pond-size': 8, 'pond-algae': 24, 'starting-towers': 1, 'starting-school': 1, 'desert': 40, 'buttes': 5,
           'springs': 6, 'wheat-amount': 100, 'wood-amount': 100, 'stone-amount': 100, 'fruit-amount': 100}
base = [r for r in rows if r['study'] == 'baseline' and r['w'] == 256 and r['ok']]
for control, metrics in SHOW.items():
    by = collections.defaultdict(list)
    for r in rows:
        if r['study'] == 'ablation' and r['control'] == control and r['ok']:
            by[r['value']].append(r)
    by[DEFAULT[control]] = base
    print(f'\n## {control} (256x256, 4 colonies, mean of {len(base)} seeds; * default)')
    print('| value | n | ' + ' | '.join(metrics) + ' |')
    print('|---|---|' + '---|' * len(metrics))
    for v in sorted(by):
        cells = []
        for m in metrics:
            vals = [METRICS[m](r) for r in by[v]]; vals = [x for x in vals if x is not None]
            cells.append(f'{st.mean(vals):.1f}' if vals else '-')
        print(f"| {v}{'*' if v == DEFAULT[control] else ''} | {len(by[v])} | " + ' | '.join(cells) + ' |')
