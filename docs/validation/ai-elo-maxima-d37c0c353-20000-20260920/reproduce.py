#!/usr/bin/env python3
"""Fit all retained duels equally; accepts additional compatible outcome archives.

No games are discarded. Duplicate IDs and incomplete side-swap blocks are errors.
Caller must keep source/settings/generator distribution comparable when pooling.
"""
import argparse
import gzip
import json
from pathlib import Path
from rating_model import summarize

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('archives', nargs='*', type=Path)
    parser.add_argument('--draws', type=int, default=1000)
    parser.add_argument('--seed', type=int, default=1)
    args = parser.parse_args()
    games = []
    for path in args.archives or [Path(__file__).with_name('outcomes.json.gz')]:
        with gzip.open(path, 'rt') as stream:
            games.extend(json.load(stream))
    print(json.dumps(summarize(games,args.draws,args.seed),indent=2,sort_keys=True))
