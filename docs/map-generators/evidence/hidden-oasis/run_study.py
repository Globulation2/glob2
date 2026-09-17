#!/usr/bin/env python3
"""Hidden Oasis parameter study: one control at a time, then random rolls of everything.
Runs the native binary in parallel and keeps one compact JSON line per map (study.jsonl).
  python3 run_study.py ablation|random [--jobs 8]
"""
import argparse, json, os, random, subprocess, sys, tempfile, time
from concurrent.futures import ThreadPoolExecutor
HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, '..', '..', '..'))
BIN = os.path.join(HERE, 'glob2')
CONTROLS = {'pond-size': list(range(5, 13)), 'pond-algae': list(range(8, 49, 4)), 'starting-towers': [0, 1],
            'starting-school': [0, 1], 'desert': list(range(0, 81, 10)), 'buttes': list(range(0, 11)),
            'springs': list(range(0, 11)), 'wheat-amount': list(range(0, 301, 25)),
            'wood-amount': list(range(0, 301, 25)), 'stone-amount': list(range(0, 301, 25)),
            'fruit-amount': list(range(0, 301, 25))}
DEFAULTS = {'pond-size': 8, 'pond-algae': 24, 'starting-towers': 1, 'starting-school': 1, 'desert': 40, 'buttes': 5,
            'springs': 6, 'wheat-amount': 100, 'wood-amount': 100, 'stone-amount': 100, 'fruit-amount': 100}

def run(job):
    fd, path = tempfile.mkstemp(suffix='.json'); os.close(fd)
    cmd = [BIN, '--generate-map', 'hidden-oasis', '--seed', str(job['seed']), '--width', str(job['w']),
           '--height', str(job['h']), '--teams', str(job['teams']), '--json', path]
    for k, v in job['set'].items():
        cmd += ['--set', f'{k}={v}']
    start = time.time()
    p = subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True)
    row = dict(job, seconds=round(time.time() - start, 3), ok=False, detail='')
    for line in (p.stdout + p.stderr).splitlines():
        if 'hidden-oasis revision' in line:
            row['ok'] = '[complete]' in line
            row['detail'] = '' if row['ok'] else line.split(']: ', 1)[-1]
    try:
        d = json.load(open(path))
        tel = {}
        for e in (d['generation'].get('telemetry') or {}).get('records', []):
            k = e['key'].replace('hidden-oasis.', '')
            if k.startswith('settlement.') or k.startswith('resources.') or k.endswith('.candidates') or 'fertility' in k:
                continue
            if e.get('kind') == 'fallback' or e.get('type') == 'fallback':
                tel.setdefault('fallbacks', []).append(k)
            else:
                tel.setdefault(k, []).append(e.get('value'))
        row['tel'] = tel
        if row['ok']:
            t = d['terrain']; r = d['resources']['types']
            row['terrain'] = {k: round(t[k]['percent'], 3) for k in ('grass', 'sand', 'water', 'grass_sand_border', 'sand_water_border')}
            row['res'] = {k: r[k]['coverage']['tiles'] for k in r}
            row['sites4x4'] = d['space']['build_sites_4x4']
            row['fert_mean'] = round(d['fertility']['all_tiles']['mean'], 1)
            row['colonies'] = len(d['map']['colonies'])
    except Exception as ex:
        row['parse'] = str(ex)[:80]
    os.unlink(path)
    return row

def ablation():
    jobs = []
    for seed in range(1, 9):
        jobs.append(dict(study='baseline', control='', value=0, seed=seed, w=256, h=256, teams=4, set={}))
        for c, values in CONTROLS.items():
            for v in values:
                if v != DEFAULTS[c]:
                    jobs.append(dict(study='ablation', control=c, value=v, seed=seed, w=256, h=256, teams=4, set={c: v}))
    for (w, h, teams) in ((128, 128, 3), (512, 512, 6), (512, 256, 5)):
        for seed in range(1, 4):
            jobs.append(dict(study='baseline', control='', value=0, seed=seed, w=w, h=h, teams=teams, set={}))
            for c, values in CONTROLS.items():
                for v in (values[0], values[-1]):
                    jobs.append(dict(study='extremes', control=c, value=v, seed=seed, w=w, h=h, teams=teams, set={c: v}))
    return jobs

def rolls(count):
    rng = random.Random(20260917)
    shapes = [(128, 128), (256, 256), (256, 256), (512, 512), (512, 256), (256, 512), (128, 256), (256, 128)]
    jobs = []
    for n in range(count):
        w, h = rng.choice(shapes)
        teams = rng.randint(2, 3 if min(w, h) == 128 else 8)
        jobs.append(dict(study='random', control='', value=0, seed=100000 + n, w=w, h=h, teams=teams,
                         set={c: rng.choice(v) for c, v in CONTROLS.items()}))
    return jobs

if __name__ == '__main__':
    ap = argparse.ArgumentParser(); ap.add_argument('which'); ap.add_argument('--jobs', type=int, default=8)
    ap.add_argument('--count', type=int, default=1500); a = ap.parse_args()
    jobs = ablation() if a.which == 'ablation' else rolls(a.count)
    out = os.path.join(HERE, a.which + '.jsonl')
    start = time.time()
    with ThreadPoolExecutor(a.jobs) as pool, open(out, 'w') as f:
        for n, row in enumerate(pool.map(run, jobs)):
            f.write(json.dumps(row) + '\n')
    print(len(jobs), 'maps in', round(time.time() - start), 's ->', out)
