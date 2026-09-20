#!/usr/bin/env python3
"""Reproduce the displayed combined Elo ratings from the retained game outcomes.

Uses only Python's standard library; run from any working directory.
"""
import gzip
import json
import random
from pathlib import Path


def rate(games):
    ratings = {}
    for game in games:
        a, b = game['competitors']
        x, y = (ratings.get(name, 1500.0) for name in (a, b))
        pa, pb = game['placements']
        score = 1.0 if pa < pb else 0.0 if pa > pb else 0.5
        delta = 32 * (score - 1 / (1 + 10 ** ((y - x) / 400)))
        ratings[a], ratings[b] = x + delta, y - delta
    return ratings


def percentile(values, p):
    values = sorted(values)
    index = p * (len(values) - 1)
    lower = int(index)
    upper = min(lower + 1, len(values) - 1)
    return values[lower] + (values[upper] - values[lower]) * (index - lower)


def summarize(games):
    assert len(games) == 10000, 'Final ratings require all 10,000 games'
    assert len({g['job_id'] for g in games}) == len(games)
    blocks = {}
    for game in games:
        blocks.setdefault(game['block'], []).append(game)
    assert len(blocks) == 5000 and all(len(block) == 2 for block in blocks.values())
    keys = sorted(blocks)
    rng = random.Random(1)
    samples = {}
    for _ in range(1000):
        draw = [game for _ in keys for game in blocks[rng.choice(keys)]]
        for name, rating in rate(draw).items():
            samples.setdefault(name, []).append(rating)
    return {
        'games': len(games), 'initial_elo': 1500, 'k': 32,
        'order': 'manifest', 'pool': 'combined macOS and Linux',
        'bootstrap_draws': 1000, 'bootstrap_seed': 1,
        'ratings': rate(games),
        'intervals_95_percent': {
            name: [percentile(values, .025), percentile(values, .975)]
            for name, values in sorted(samples.items())},
    }


if __name__ == '__main__':
    with gzip.open(Path(__file__).with_name('outcomes.json.gz'), 'rt') as stream:
        games = json.load(stream)
    print(json.dumps(summarize(games), indent=2, sort_keys=True))
