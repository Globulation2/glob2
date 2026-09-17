#!/usr/bin/env python3
"""Pool the 4 size-group AI-comparison results directories into one overall
Elo leaderboard per format. Reuses tools.tournaments.analysis unchanged --
Elo is computed per-format already, so games from every map size can be
pooled into a single rate() call.

Usage: python3 tools/tournaments_ai_leaderboard.py RESULTS_DIR [RESULTS_DIR ...] [--policy prestige] [--output report.json]
"""
import argparse
import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from tools.tournaments.analysis import observations, rate, bootstrap_ratings, POLICIES
from tools.tournaments.results import Results


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('directories', nargs='+', help='one results directory per size group')
    parser.add_argument('--policy', choices=POLICIES, default='prestige')
    parser.add_argument('--k', type=float, default=32)
    parser.add_argument('--draws', type=int, default=1000)
    parser.add_argument('--seed', type=int, default=1)
    parser.add_argument('--output')
    args = parser.parse_args()

    rows, all_jobs, engine_decided, capped = [], [], 0, 0
    for directory in args.directories:
        source = Results(directory)
        directory_rows = observations(list(source), args.policy)
        rows += directory_rows
        all_jobs += source.manifest['jobs']
        engine_decided += sum(row['engine_outcome'] for row in directory_rows)
        capped += sum(row['cap'] for row in directory_rows)

    ratings = rate(rows, args.k)
    total = len(rows)
    report = {
        'policy': args.policy, 'k': args.k,
        'directories': args.directories,
        'games': total,
        'engine_decided': engine_decided,
        'capped_and_adjudicated': capped,
        'ratings': ratings,
    }
    if total:
        report['uncertainty'] = bootstrap_ratings(rows, {'jobs': all_jobs}, args.draws, args.seed, args.k)

    text = json.dumps(report, indent=2)
    if args.output:
        Path(args.output).write_text(text)
    print(text)

    print(f'\n{total} observed games ({engine_decided} engine-decided, {capped} capped-and-adjudicated)\n',
          file=sys.stderr)
    print('| Format | Competitor | Elo |', file=sys.stderr)
    print('| --- | --- | ---: |', file=sys.stderr)
    for fmt, fmt_ratings in sorted(ratings.items()):
        for competitor, value in sorted(fmt_ratings.items(), key=lambda item: (-item[1], item[0])):
            print(f'| {fmt} | {competitor} | {value:.1f} |', file=sys.stderr)


if __name__ == '__main__':
    main()
