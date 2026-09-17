#!/usr/bin/env python3
"""Random-sample AI Elo tournament: K games, each independently drawing its
own format, AI matchup, map generator and map size, rather than an exhaustive
sweep. Uses the engine's inline map-generation path (one 'game' job embeds its
own --generator/--map-seed/--param), so each sample is a single job with no
separate map-generation dependency.

Usage: python3 tools/tournaments_ai_elo_random_sample.py K --bundle DIR --output RESULTS [--seed 1] [--ticks 90000]
"""
import argparse
import json
import random
import re
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from tools.tournaments.bundles import inspect_bundle
from tools.tournaments.coordinator import Coordinator
from tools.tournaments.model import job, validate_experiment

SIZES = [(7, 7, '128x128'), (7, 8, '128x256'), (8, 8, '256x256'), (8, 7, '256x128')]
FORMATS = ['1v1', '2v2', 'ffa']


def playable_generators(binary):
    out = subprocess.check_output([binary, '--list-map-generators'], text=True)
    gens = []
    for line in out.splitlines():
        m = re.match(r'(\S+) \(id (\d+), revision \d+(, editor only)?\)', line)
        if not m or m.group(3):
            continue
        gens.append(int(m.group(2)))
    return sorted(gens)


def sample_job(rng, ais, generators, ticks):
    fmt = rng.choice(FORMATS)
    n = 2 if fmt == '1v1' else 4
    width, height, size_label = rng.choice(SIZES)
    generator = rng.choice(generators)
    map_seed = rng.getrandbits(32)
    game_seed = rng.getrandbits(32)
    if fmt in ('1v1', 'ffa'):
        players = rng.sample(ais, n)
        alliances = None
    else:
        a, b = rng.sample(ais, 2)
        players = [a, a, b, b]
        alliances = [1, 1, 2, 2]
    config = {
        'generator': generator, 'params': {'width': width, 'height': height, 'teams': n},
        'candidates': 5, 'players': players, 'ticks': ticks, 'ai_params': {},
    }
    if alliances:
        config['alliances'] = alliances
    labels = {
        'format': fmt, 'generator': generator, 'map_seed': map_seed,
        'map': f'{generator}:{map_seed}', 'size': size_label, 'rotation': 0,
        'variant': 'baseline', 'block': f'{generator}:{map_seed}:{game_seed}',
        'symmetric_control': generator == 15,
    }
    return job('game', BUNDLE_ID, seeds={'map': map_seed, 'game': game_seed},
               config=config, limits={'timeout_seconds': 3600}, labels=labels)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('count', type=int, help='number of randomly-sampled games (K)')
    parser.add_argument('--bundle', required=True)
    parser.add_argument('--output', required=True)
    parser.add_argument('--seed', type=int, default=1)
    parser.add_argument('--ticks', type=int, default=90000)
    parser.add_argument('--list-generators-binary', default='build/src/glob2',
                         help='local, executable binary of the same revision, used only to enumerate generators')
    args = parser.parse_args()

    bundle = inspect_bundle(args.bundle)
    global BUNDLE_ID
    BUNDLE_ID = bundle['id']
    ais = [a['name'] for a in bundle['capabilities']['ais'] if a['id'] != 0]
    generators = playable_generators(args.list_generators_binary)
    if not ais or not generators:
        raise SystemExit('bundle capabilities missing AIs or the binary has no playable generators')

    rng = random.Random(args.seed)
    jobs = [sample_job(rng, ais, generators, args.ticks) for _ in range(args.count)]
    jobs = list({j['id']: j for j in jobs}.values())  # drop exact-duplicate draws, if any
    manifest = {'schema_version': 1, 'id': f'ai-elo-random-{args.count}', 'jobs': jobs,
                'settings': {}, 'labels': {'seed': args.seed}, 'design': {
                    'kind': 'random_sample', 'count': args.count, 'seed': args.seed, 'ticks': args.ticks}}
    validate_experiment(manifest)

    coordinator = Coordinator.submit(args.output, manifest, [args.bundle])
    coordinator.close()
    print(json.dumps({'jobs': len(jobs), 'ais': ais, 'generators': len(generators), 'output': args.output}, indent=2))


if __name__ == '__main__':
    main()
