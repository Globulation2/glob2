#!/usr/bin/env python3
"""Per-colony counters from headless games' GLOB2_MEASURE lines: did the map's MECHANISM happen?

game_economy.py answers "did the colonies eat"; this answers "did they mine the prize, resupply the
tower, finish the school, lose the wall". Run games with `--telemetry team-timeline` (see game_economy.py)
and pass the logs; result.json is looked for in the directory named like the log without ".log".

  python3 game_counters.py LOG [LOG...] [--keys harvested_4 delivered_4 shots_2 consumed_2_3 ...]

Counter names (src/TeamStat.h, GameplayMeasurements), R = resource (0 wood, 1 wheat, 3 stone, 4 algae,
5-7 fruit), T = building short type (0 swarm, 1 inn, 2 hospital, 3 racetrack, 4 pool, 5 barracks,
6 school, 7 tower, 11 wall, 12 market), L = long level (2 x level, +1 when finished: 1 is a finished
level-1 building, 3 a finished level-2):
  harvested_R / delivered_R   picked up / brought home: a prize harvested but never delivered is too far
  consumed_P_R                spent by purpose P (0 meal, 1 spawning, 2 ammunition, 3 construction,
                              4 upgrade): consumed_2_3 is stone fired by towers, above a granted
                              tower's starting reserve only if somebody resupplied it
  shots_S, damageDealt_S_G    by source S (0 melee, 1 magic, 2 tower) on target G (0 unit, 1 building)
  deaths_U_C                  unit U (0 worker, 1 explorer, 2 warrior) by cause C (0 combat, 1 starvation)
  buildings_T_L               standing now; completed_K_T_L built (K 0 new, 1 upgraded, 2 repaired);
                              removed_K_T_L lost (K 0 destroyed, 1 demolished)
  abilityGains_U_A            training: abilityGains_0_6 is workers gaining build level
Pair every game with the mechanism switched off (towers off, prize open) on the same map and seed:
the difference is the mechanism, the rest is the map.
"""
import argparse, json, os, re
DEFAULT = ['harvested_4', 'delivered_4', 'harvested_3', 'deaths_0_0', 'deaths_0_1', 'deaths_2_0', 'shots_2',
           'consumed_2_3', 'damageDealt_0_1', 'buildings_6_1', 'buildings_7_1', 'removed_0_7_1', 'abilityGains_0_6']
ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
ap.add_argument('logs', nargs='+'); ap.add_argument('--keys', nargs='*', default=DEFAULT)
a = ap.parse_args()
for log in a.logs:
    final = {}
    for line in open(log, errors='replace'):
        if line.startswith('GLOB2_MEASURE'):
            v = dict(re.findall(r'(\S+?)=(-?\d+)', line)); final[int(v['team'])] = v
    result = log[:-4] + '/result.json'
    teams = {t['team']: t for t in json.load(open(result)).get('teams', [])} if os.path.exists(result) else {}
    print('==', os.path.basename(log))
    print('team  peak   end  ' + '  '.join(a.keys) + '  eliminated')
    for k in sorted(final):
        units = [row[0] for row in teams.get(k, {}).get('history', [])] or [0]
        print(f'{k:>4} {max(units):>5} {units[-1]:>5}  ' +
              '  '.join(f"{final[k].get(key, '?'):>{len(key)}}" for key in a.keys) +
              f"  {teams.get(k, {}).get('eliminated_tick')}")
