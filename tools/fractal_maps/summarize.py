#!/usr/bin/env python3
"""Summarize complete, explicitly selected seat blocks from analyzed experiments.

Input directories are outputs of analyze.py. Optional --jobs names a JSON array of
job IDs. Without it every game is included, and duplicate seats are an error rather
than an implicit choice of the better result. Keep two- and four-colony studies in
separate invocations with their required --rotations count.
"""
import argparse
from collections import defaultdict
import csv
import json
from pathlib import Path
from .analyze import write_csv


def summarize(inputs, output, rotations, wanted=None):
    games, teams = [], []
    for directory in inputs:
        for filename, target in [('jobs.csv', games), ('teams.csv', teams)]:
            with (Path(directory)/filename).open() as stream:
                for row in csv.DictReader(stream):
                    if row['kind'] == 'game' and (wanted is None or row['job'] in wanted):
                        row['analysis_source'] = str(directory)
                        target.append(row)
    found = {g['job'] for g in games}
    grouped = defaultdict(list)
    for game in games:
        grouped[(game['build'], game['ai'], game['generator_key'], game['seed'])].append(game)
    by_job = defaultdict(list)
    for team in teams:
        by_job[team['job']].append(team)
    blocks = []
    for (build, ai, generator, seed), records in sorted(grouped.items()):
        seats = [int(g['rotation']) for g in records]
        if len(set(seats)) != len(seats):
            raise ValueError(f'duplicate seat in {build}/{ai}/{generator}/{seed}; select exact job IDs')
        members = [t for game in records for t in by_job[game['job']]]
        # No cap adjudication and no pooling across builds/seeds. The source team
        # rows remain available for every seat-specific observation in the report.
        block = dict(build=build, ai=ai, generator=generator, seed=seed,
                     games=len(records), required_games=rotations,
                     complete=set(seats)==set(range(rotations)),
                     successful=sum(g['category']=='success' for g in records),
                     committed=sum(g.get('committed')=='True' for g in records),
                     capped=sum(g['termination']=='tick_cap' for g in records),
                     engine_ended=sum(g['termination']=='engine_end' for g in records),
                     missing_telemetry=sum(not g.get('telemetry_teams') or g['telemetry_teams']=='0' for g in records))
        for field in ['peak_population','opening_starvation','final_starvation',
                      'final_buildings_completed','final_upgrades_completed','first_observed_combat']:
            values = [float(t[field]) for t in members if t.get(field)]
            block[field+'_observations'] = len(values)
            if values:
                block[field+'_min'] = min(values)
                block[field+'_max'] = max(values)
                block[field+'_mean'] = sum(values)/len(values)
        blocks.append(block)
    output.mkdir(parents=True, exist_ok=True)
    write_csv(output/'games.csv',games);write_csv(output/'teams.csv',teams);write_csv(output/'blocks.csv',blocks)
    summary = dict(games=len(games), teams=len(teams), blocks=len(blocks),
                   complete_blocks=sum(b['complete'] for b in blocks),
                   missing_requested_jobs=sorted((wanted or set())-found),
                   rotations=rotations)
    (output/'summary.json').write_text(json.dumps(summary,indent=2)+'\n')
    print(json.dumps(summary,indent=2))


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('analysis',nargs='+');p.add_argument('--output',required=True)
    p.add_argument('--rotations',type=int,choices=[2,4],required=True)
    p.add_argument('--jobs',help='JSON array selecting exact job IDs across inputs')
    a=p.parse_args()
    wanted=set(json.loads(Path(a.jobs).read_text())) if a.jobs else None
    summarize(a.analysis,Path(a.output),a.rotations,wanted)
