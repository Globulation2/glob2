#!/usr/bin/env python3
"""Summarise one headless game's economy per colony, and optionally an AI's internal values.

Play a quick calibration game with telemetry (sizes are exponents; `--candidates 5` picks the
roll the lobby would):

  build/src/glob2 --generate-map --generator 39 --map-seed 101 --param teams=4 --param width=8 \
      --param height=8 --candidates 5 --write-map true --output-dir /tmp/gen
  SDL_VIDEODRIVER=dummy build/src/glob2 --run-game --map-file /tmp/gen/map-r0.map --game-seed 1 \
      --player nicowar --player nicowar --player nicowar --player nicowar --ticks 20000 \
      --telemetry team-timeline --save final --output-dir /tmp/play > /tmp/play.log 2>&1
  python3 game_economy.py /tmp/play.log --result /tmp/play/result.json --ai <telemetry name fragments>

Play-test with the newer AIs (Nicowar, Cortex, Cabino, Maxima); the older Numbi, Castor and
Warrush are not tuning targets (references/tuning-playbook.md, "Which AIs to play").

Prints, per team, the final GLOB2_MEASURE counters (births, wheat and wood harvested, starvation and
combat deaths by unit type, hungry units), the unit history every 4,096 ticks and the building
count, and with --ai every GLOB2_AI_FINAL value whose name contains one of the given words.
"""
import argparse
import json
import re

parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
parser.add_argument('log')
parser.add_argument('--result', help="the game's result.json, for unit history and buildings")
parser.add_argument('--ai', nargs='*', default=[], help='substrings of AI telemetry names to print')
args = parser.parse_args()
measure, ai = {}, {}
for line in open(args.log, errors='replace'):
    if line.startswith('GLOB2_MEASURE'):
        values = dict(re.findall(r'(\S+?)=(-?\d+)', line))
        measure[int(values['team'])] = values
    elif line.startswith('GLOB2_AI_FINAL'):
        values = dict(re.findall(r'(\S+?)=(\S+)', line))
        ai[int(values['team'])] = values
keys = ['tick', 'births_0', 'harvested_1', 'harvested_0', 'deaths_0_1', 'deaths_2_1', 'deaths_0_0',
        'deaths_2_0', 'hungry', 'critical']
names = {'births_0': 'worker births', 'harvested_1': 'wheat', 'harvested_0': 'wood',
         'deaths_0_1': 'workers starved', 'deaths_2_1': 'warriors starved',
         'deaths_0_0': 'workers killed', 'deaths_2_0': 'warriors killed'}
for team in sorted(measure):
    print(f'team {team}: ' + ', '.join(f'{names.get(k, k)} {measure[team].get(k, "?")}' for k in keys))
    if args.ai and team in ai:
        print('   ' + ' '.join(f'{k}={v}' for k, v in ai[team].items()
                                 if '@' not in k and any(word in k for word in args.ai)))
if args.result:
    for team in json.load(open(args.result)).get('teams', []):
        history = team.get('history', [])
        print(f"team {team['team']}: units every 4096 ticks {[row[0] for row in history[::8]]}, "
              f"buildings {team.get('buildings')}, eliminated at {team.get('eliminated_tick')}")
