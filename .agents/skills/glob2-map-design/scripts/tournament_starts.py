#!/usr/bin/env python3
"""Per-start economy of a rotation tournament, pooled over rotations, before any win count.

  python3 tournament_starts.py RESULTS [--detail GENERATOR] [--telemetry-key glacis.fort.facing]

RESULTS is a `tools.tournaments` results directory (run from the repository root, or put it on
PYTHONPATH). For every generator: mean peak and final units, buildings, births, wheat and wood
harvested, starvation and combat deaths per colony-game, and eliminations; then the weakest and
strongest (map seed, start) pairs. With --detail, every (map, start) row for that generator, and
with --telemetry-key, the value its generation job recorded for that key and colony (for example a
per-colony facing), so a result that splits by start can be traced to the design draw behind it.
Reading the final GLOB2_MEASURE rows verifies every log artifact, which takes a minute or two.
"""
import argparse
import statistics
import sys
from collections import defaultdict
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[4]))
from tools.tournaments.results import Results  # noqa: E402

parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
parser.add_argument('results')
parser.add_argument('--detail', type=int, help='generator id to list start by start')
parser.add_argument('--telemetry-key', help="a generation telemetry key recorded per colony")
args = parser.parse_args()
source = Results(args.results)
records = list(source)
design = {}
if args.telemetry_key:
    for record in records:
        if record.get('job', {}).get('type') == 'generate_map' and record.get('category') == 'success':
            report = record['result'].get('map_report') or {}
            label = record['job']['labels']
            for row in report.get('generation', {}).get('telemetry', {}).get('records', []):
                if row['key'] == args.telemetry_key:
                    design[(label.get('generator'), label.get('map_seed'), row['subject'])] = row['value']
rows = []
for record in records:
    if record.get('job', {}).get('type') != 'game' or record.get('category') != 'success':
        continue
    label, result = record['job']['labels'], record['result']
    final = {}
    for row in source.telemetry(record):
        if row.get('record') == 'GLOB2_MEASURE' and row['values'].get('final'):
            final[row['values']['team']] = row['values']
    count = len(result['teams'])
    for team in result['teams']:
        values, history = final.get(team['team'], {}), team['history']
        rows.append(dict(
            generator=label['generator'], map=label['map_seed'],
            start=(team['team'] - label['rotation']) % count,
            peak=max(h[0] for h in history), final=history[-1][0], buildings=team['buildings'],
            eliminated=team['eliminated_tick'] >= 0,
            births=sum(values.get(f'births_{u}', 0) for u in range(3)),
            wheat=values.get('harvested_1', 0), wood=values.get('harvested_0', 0),
            starved=sum(values.get(f'deaths_{u}_1', 0) for u in range(3)),
            killed=sum(values.get(f'deaths_{u}_0', 0) for u in range(3))))
by_generator = defaultdict(list)
for row in rows:
    by_generator[row['generator']].append(row)
for generator, group in sorted(by_generator.items()):
    mean = lambda key: statistics.mean(r[key] for r in group)
    print(f"generator {generator}: peak {mean('peak'):.0f}, final {mean('final'):.0f}, buildings "
          f"{mean('buildings'):.0f}, births {mean('births'):.0f}, wheat {mean('wheat'):.0f}, wood "
          f"{mean('wood'):.0f}, starved {mean('starved'):.1f}, killed {mean('killed'):.1f}, "
          f"eliminated {sum(r['eliminated'] for r in group)}/{len(group)}")
    starts = defaultdict(list)
    for row in group:
        starts[(row['map'], row['start'])].append(row)
    ranked = sorted((statistics.mean(r['peak'] for r in v), k) for k, v in starts.items())
    print('   weakest (map, start) by mean peak', [(k, round(p)) for p, k in ranked[:3]],
          'strongest', [(k, round(p)) for p, k in ranked[-3:]])
    if args.detail == generator:
        for key in sorted(starts):
            draw = design.get((generator, key[0], key[1]), '') if args.telemetry_key else ''
            print(f'   {key} {draw}', [(r['peak'], r['buildings'], r['eliminated']) for r in starts[key]])
