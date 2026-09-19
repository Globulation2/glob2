#!/usr/bin/env python3
"""Retain final mixed-AI game counters. Run with the raw evidence directory."""
import csv, json, re, sys
from pathlib import Path
root = Path(sys.argv[1])
rows = []
for path in sorted(root.glob('v9-game-s*-r*.log')):
    match = re.fullmatch(r'v9-game-s(\d+)-r(\d+)\.log', path.name)
    seed, rotation = map(int, match.groups())
    result_path = path.with_suffix('') / 'result.json'
    if not result_path.exists():
        continue
    result = json.loads(result_path.read_text())
    counters = {}
    for line in path.read_text().splitlines():
        if line.startswith('GLOB2_MEASURE'):
            data = {k: int(v) for k, v in re.findall(r'(\S+?)=(-?\d+)', line)}
            counters[data['team']] = data
    for team in result['teams']:
        k = team['team']
        c = counters[k]
        history = team['history']
        rows.append(dict(seed=seed, rotation=rotation, start=(k-rotation)%4,
            team=k, ai=result['players'][k]['ai'], ticks=result['ticks'],
            peak_units=max(h[0] for h in history), final_units=history[-1][0],
            buildings=team['buildings'], eliminated_tick=team['eliminated_tick'],
            worker_births=c['births_0'], wheat=c['harvested_1'], wood=c['harvested_0'],
            workers_starved=c['deaths_0_1'], warriors_starved=c['deaths_2_1'],
            workers_killed=c['deaths_0_0'], warriors_killed=c['deaths_2_0'],
            tower_shots=c.get('shots_2',0), stone_delivered=c.get('delivered_3',0),
            ammunition_stone=c.get('consumed_2_3',0)))
if rows:
    writer=csv.DictWriter(sys.stdout, fieldnames=rows[0].keys())
    writer.writeheader();writer.writerows(rows)
