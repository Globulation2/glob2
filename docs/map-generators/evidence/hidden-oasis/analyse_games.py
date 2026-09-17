#!/usr/bin/env python3
"""Per-team summary of Hidden Oasis playtests from GLOB2_MEASURE (final line per team) and result.json.
Usage: analyse_games.py LOG [LOG...]   (result.json is looked up beside each log's output dir)"""
import json, os, re, sys
for log in sys.argv[1:]:
    m = {}
    for line in open(log, errors='replace'):
        if line.startswith('GLOB2_MEASURE'):
            v = dict(re.findall(r'(\S+?)=(-?\d+)', line)); m[int(v['team'])] = v
    res = log[:-4] + '/result.json'
    teams = {t['team']: t for t in json.load(open(res)).get('teams', [])} if os.path.exists(res) else {}
    print('==', os.path.basename(log))
    print('team  peak  end  algaeH algaeDeliv  wkill wstarv warkill  towerShots ammoStone  meleeOnBldg  schools(0/1/2) towersLost  elim')
    for k in sorted(m):
        v = m[k]; g = lambda n: int(v.get(n, 0))
        hist = [r[0] for r in teams.get(k, {}).get('history', [])] or [0]
        schools = '/'.join(str(g(f'buildings_6_{2*l+1}') + 0) for l in range(3))
        print(f"{k:>4} {max(hist):>5} {hist[-1]:>4} {g('harvested_4'):>7} {g('delivered_4'):>10} "
              f"{g('deaths_0_0'):>6} {g('deaths_0_1'):>6} {g('deaths_2_0'):>7}  {g('shots_2'):>10} {g('consumed_2_3'):>9} "
              f"{g('damageDealt_0_1'):>10}  {schools:>12} {g('removed_0_7_1')+g('removed_0_7_3')+g('removed_0_7_5'):>9}  {teams.get(k,{}).get('eliminated_tick')}")
