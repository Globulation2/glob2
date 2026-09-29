#!/usr/bin/env python3
"""Summarize the agreed 128–512 tile, 2–8 colony default support envelope.

The stress planner includes one extra default request per seed. Deduplicate that
identical request when counting seed evidence, while reporting any disagreement
between its repeats as a reproducibility failure.
"""
import argparse
from collections import Counter, defaultdict
import csv
import json
from pathlib import Path


def summarize(jobs_csv, output):
    seeds = set(range(20001, 20017)) | set(range(30001, 30009))
    outcomes = defaultdict(set)
    with jobs_csv.open(newline='') as stream:
        for row in csv.DictReader(stream):
            if row['study'] not in ('service-bulk-training', 'service-bulk-held-out'):
                continue
            key = (int(row['generator']), int(row['width']), int(row['height']),
                   int(row['teams']), int(row['seed']))
            outcomes[key].add(row['category'])
    rows = []
    classifications = Counter()
    repeats_conflicting = []
    for generator in (34, 35):
        for width in (7, 8, 9):
            for height in (7, 8, 9):
                for teams in range(2, 9):
                    states = defaultdict(list)
                    for seed in sorted(seeds):
                        values = outcomes.get((generator, width, height, teams, seed), set())
                        if len(values) > 1:
                            repeats_conflicting.append(dict(generator=generator, width=width,
                                                            height=height, teams=teams, seed=seed,
                                                            categories=sorted(values)))
                        states[next(iter(values)) if len(values) == 1 else
                               ('conflict' if values else 'missing')].append(seed)
                    if states['missing']:
                        classification = 'pending'
                    elif states['conflict']:
                        classification = 'conflicting-repeat'
                    elif len(states['success']) == len(seeds):
                        classification = 'supported'
                    elif len(states['invalid_request']) == len(seeds):
                        classification = 'unsupported'
                    elif any(states[k] for k in states if k not in
                             ('success', 'invalid_request', 'missing', 'conflict')):
                        classification = 'generation-failure'
                    else:
                        classification = 'mixed-seed-outcome'
                    classifications[classification] += 1
                    rows.append(dict(generator=generator, width_tiles=1 << width,
                                     height_tiles=1 << height, teams=teams,
                                     classification=classification,
                                     successful_seeds=','.join(map(str, states['success'])),
                                     rejected_seeds=','.join(map(str, states['invalid_request'])),
                                     other_seeds=','.join(map(str, sorted(seed for category, values
                                               in states.items() if category not in
                                               ('success', 'invalid_request', 'missing')
                                               for seed in values))),
                                     missing_seeds=','.join(map(str, states['missing']))))
    output.mkdir(parents=True, exist_ok=True)
    with (output / 'default-envelope.csv').open('w', newline='') as stream:
        writer = csv.DictWriter(stream, rows[0].keys())
        writer.writeheader()
        writer.writerows(rows)
    summary = dict(combinations=len(rows), planned_seeds_per_combination=len(seeds),
                   classifications=dict(classifications), conflicting_repeats=repeats_conflicting)
    (output / 'default-envelope-summary.json').write_text(json.dumps(summary, indent=2) + '\n')
    print(json.dumps({**summary, 'conflicting_repeats': len(repeats_conflicting)}, indent=2))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('jobs_csv', type=Path)
    parser.add_argument('--output', required=True, type=Path)
    args = parser.parse_args()
    summarize(args.jobs_csv, args.output)
