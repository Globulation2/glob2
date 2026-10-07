#!/usr/bin/env python3
"""pc.py <base> <arm>...: petri_construction per island, paired over seeds.

Per team: mean tick at which its k-th building was completed (512-tick resolution,
missing completions count as the run length), completions by the end, deliveries
and starvation deaths.
"""
import glob, os, re, sys

ISLAND = ['feed+fortify', 'finish/start', 'near/far', 'two shores', 'priority']

def parse(path):
    teams = {}
    end = 0
    for line in open(path):
        if line.startswith('GLOB2_MEASURE_HISTORY') or line.startswith('GLOB2_MEASURE '):
            kv = dict(x.split('=', 1) for x in line.split()[1:] if '=' in x)
            tick = int(kv['tick']); end = max(end, tick)
            c = sum(int(v) for k, v in kv.items() if re.fullmatch(r'completed_0_\d+_\d+', k))
            d = sum(int(v) for k, v in kv.items() if re.fullmatch(r'delivered_\d+', k))
            s = sum(int(v) for k, v in kv.items() if re.fullmatch(r'deaths_\d+_1', k))
            t = teams.setdefault(kv['team'], {'times': [], 'c': 0, 'd': 0, 's': 0})
            while len(t['times']) < c:
                t['times'].append(tick)
            t['c'] = max(t['c'], c); t['d'] = max(t['d'], d); t['s'] = max(t['s'], s)
    return teams, end

def load(arm):
    out = {}
    for f in glob.glob(f'/work/carol/hire/runs/pc/{arm}/*/stdout.log'):
        if os.path.exists(os.path.join(os.path.dirname(f), 'done')):
            out[f.split('/')[-2]] = parse(f)
    return out

def summary(runs, team, n):
    mean_k, comp, deliv, starv = [], 0, 0, 0
    for teams, end in runs:
        t = teams.get(team, {'times': [], 'c': 0, 'd': 0, 's': 0})
        times = t['times'] + [end] * max(0, n - len(t['times']))
        mean_k.append(sum(times[:n]) / n)
        comp += t['c']; deliv += t['d']; starv += t['s']
    k = len(runs)
    return sum(mean_k) / k, comp / k, deliv / k, starv / k

arms = sys.argv[1:]
data = {a: load(a) for a in arms}
seeds = sorted(set.intersection(*[set(d) for d in data.values()]), key=int)
print(f'{len(seeds)} paired seeds')
NEED = {'0': 8, '1': 8, '2': 8, '3': 9, '4': 9}
for team in sorted(NEED):
    print(f'team {team} {ISLAND[int(team)]}: mean completion tick of the first {NEED[team]} | built | deliveries | starved')
    for a in arms:
        m, c, d, s = summary([data[a][x] for x in seeds], team, NEED[team])
        print(f'   {a:8s} {m:8.0f} | {c:5.2f} | {d:7.1f} | {s:5.2f}')
