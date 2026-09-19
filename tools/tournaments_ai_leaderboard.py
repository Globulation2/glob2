#!/usr/bin/env python3
"""Pool several AI-comparison results directories into one leaderboard per format.

Reports two numbers per competitor, because they answer different questions.

`elo` is the existing iterative rating from tools.tournaments.analysis, kept
unchanged. It is what the tournament reports as it goes, but it understates how
far apart the strongest and weakest actually are: a rating that walks by K per
game cannot reach the spread implied by a matchup one side never loses, and no
finite Elo can express a 100% win rate at all. It also depends on the order the
games happened to be played.

`strength` fits every game at once instead, by maximum likelihood over the whole
finishing order (Plackett-Luce, the Bradley-Terry model when there are two
sides). Order does not matter, every game informs every rating, and the ridge
penalty keeps a competitor who has never lost at a finite number rather than
letting it run away. It is reported on the same 400-points-per-tenfold scale as
Elo, centred on 1500, so the two columns can be read side by side -- expect
`strength` to be the wider and the more faithful of the two.

Usage: python3 tools/tournaments_ai_leaderboard.py RESULTS_DIR [RESULTS_DIR ...] [--policy prestige] [--output report.json]
"""
import argparse
import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from collections import defaultdict
import math

from tools.tournaments.analysis import observations, rate, bootstrap_ratings, POLICIES
from tools.tournaments.results import Results

# Elo's scale: a rating difference of this many points is a factor of ten in the
# odds, which is what turns a fitted log-odds strength into Elo-comparable points.
ELO_POINTS_PER_LOG_ODDS = 400.0 / math.log(10.0)


def strengths(rows, ridge=1e-2):
    """Per-format strengths fitted to every game at once, in Elo-like points.

    Each game is one contest and each competitor one entry, measured only by an
    indicator saying which competitor it is; the fitted coefficient is then that
    competitor's strength. This is the same estimator the map fairness and win
    probability models use (tools/conditional_logit.py), which is why it is a fit
    here and not a second implementation of one.

    Softmax is invariant to adding a constant to every strength, so the level is
    fixed by convention -- centred, then offset to 1500 -- and only differences
    carry meaning. The ridge penalty is what keeps an unbeaten competitor finite:
    without it the likelihood is maximised by sending its strength to infinity.
    """
    try:
        import numpy  # noqa: F401
        import scipy.optimize  # noqa: F401
    except ImportError:
        return None
    from tools.conditional_logit import fit_model

    by_format = defaultdict(list)
    for row in rows:
        by_format[row['format']].append(row)
    report = {}
    for fmt, games in sorted(by_format.items()):
        names = sorted({name for row in games for name in row['competitors']})
        if len(names) < 2:
            continue
        dataset = {'games': [
            {'map': row['map'], 'job_id': row['job_id'],
             'entries': [{'measurements': {name: 1.0}, 'placement': placement,
                          'won': name in row['winners']}
                         for name, placement in zip(row['competitors'], row['placements'])]}
            for row in games]}
        model = fit_model(dataset, [(name, 'identity') for name in names], ridge)
        raw = {item['name']: item['coefficient'] for item in model['features']}
        centre = sum(raw.values()) / len(raw)
        report[fmt] = {name: 1500.0 + (value - centre) * ELO_POINTS_PER_LOG_ODDS
                       for name, value in raw.items()}
        report[fmt + ':fit'] = {'games': len(games), 'ridge': ridge,
                                'accuracy': model['train'].get('accuracy'),
                                'chance_accuracy': model['train'].get('chance_accuracy')}
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('directories', nargs='+', help='one results directory per size group')
    parser.add_argument('--policy', choices=POLICIES, default='prestige')
    parser.add_argument('--k', type=float, default=32)
    parser.add_argument('--draws', type=int, default=1000)
    parser.add_argument('--seed', type=int, default=1)
    parser.add_argument('--ridge', type=float, default=1e-2,
                        help='penalty holding an unbeaten competitor to a finite strength')
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
    fitted = strengths(rows, args.ridge) if total else None
    if fitted is None and total:
        print('strength fit skipped: needs numpy and scipy', file=sys.stderr)
    elif fitted:
        report['strengths'] = fitted
    if total:
        report['uncertainty'] = bootstrap_ratings(rows, {'jobs': all_jobs}, args.draws, args.seed, args.k)

    text = json.dumps(report, indent=2)
    if args.output:
        Path(args.output).write_text(text)
    print(text)

    print(f'\n{total} observed games ({engine_decided} engine-decided, {capped} capped-and-adjudicated)\n',
          file=sys.stderr)
    print('| Format | Competitor | Elo | Strength |', file=sys.stderr)
    print('| --- | --- | ---: | ---: |', file=sys.stderr)
    for fmt, fmt_ratings in sorted(ratings.items()):
        # rate() keys each cohort as "format:build"; the fit deliberately pools
        # the builds, so the strength is looked up by the bare format.
        fitted_fmt = (report.get('strengths') or {}).get(fmt.split(':', 1)[0], {})
        order = sorted(fmt_ratings, key=lambda name: (-fitted_fmt.get(name, fmt_ratings[name]), name))
        for competitor in order:
            strength = fitted_fmt.get(competitor)
            shown = f'{strength:.0f}' if strength is not None else '-'
            print(f'| {fmt} | {competitor} | {fmt_ratings[competitor]:.1f} | {shown} |',
                  file=sys.stderr)


if __name__ == '__main__':
    main()
