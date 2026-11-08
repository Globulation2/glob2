#!/usr/bin/env python3
"""Compare two simulation fingerprints and report the first difference.

    python3 tools/sim_diff.py base.json.gz candidate.json.gz [--json verdict.json]

Bundles come from tools/sim_fingerprint.py. Exit status 0 means every scenario
matched, 1 that the simulation output differs (the first difference of each
scenario is printed: scenario, tick and component), 2 that the bundles cannot be
compared. Equal output does not prove behaviour unchanged: the scenarios cover
only what they run.
"""
from __future__ import annotations

import argparse
import gzip
import json
import sys
from pathlib import Path


def load(path: Path) -> dict:
    with gzip.open(path, 'rt') as stream:
        return json.load(stream)


def first_game_difference(base: list, cand: list) -> dict | None:
    for index, (b, c) in enumerate(zip(base, cand)):
        if b == c:
            continue
        tick = b[0]
        for team, (bt, ct) in enumerate(zip(b[2], c[2])):
            for part, name in enumerate(('team checksum', 'units', 'buildings')):
                if bt[part] != ct[part]:
                    return {'tick': tick, 'component': f'team {team} {name}'}
        if len(b[2]) != len(c[2]):
            return {'tick': tick, 'component': 'team count'}
        # Teams and entities agree: the aggregate covers the map, scripts,
        # players and the map header.
        return {'tick': tick, 'component': 'aggregate only (map, scripts, players or header)'}
    if len(base) != len(cand):
        return {'tick': (base if len(base) > len(cand) else cand)[min(len(base), len(cand))][0],
                'component': f'tick count {len(base)} vs {len(cand)}'}
    return None


def first_trace_difference(base: list, cand: list) -> dict | None:
    for (bt, bc), (ct, cc) in zip(base, cand):
        if (bt, bc) != (ct, cc):
            return {'tick': bt, 'component': 'state checksum'}
    if len(base) != len(cand):
        return {'tick': None, 'component': f'trace length {len(base)} vs {len(cand)}'}
    return None


def compare(base: dict, cand: dict) -> list[dict]:
    if base.get('schema') != cand.get('schema'):
        raise ValueError(f'bundle schemas differ: {base.get("schema")} vs {cand.get("schema")}')
    if base.get('ticks') != cand.get('ticks'):
        raise ValueError(f'bundles ran different tick counts: {base.get("ticks")} vs {cand.get("ticks")}')
    differences = []
    names = sorted(set(base['scenarios']) | set(cand['scenarios']))
    for name in names:
        b, c = base['scenarios'].get(name), cand['scenarios'].get(name)
        if b is None or c is None:
            differences.append({'scenario': name, 'tick': None,
                                'component': 'only in ' + ('candidate' if b is None else 'base')})
            continue
        if b['kind'] == 'game':
            found = first_game_difference(b['ticks'], c['ticks'])
        elif b['kind'] == 'match':
            found = first_trace_difference(b['ticks'], c['ticks'])
        else:
            changed = sorted(key for key in set(b['maps']) | set(c['maps'])
                             if b['maps'].get(key) != c['maps'].get(key))
            found = {'tick': None, 'component': f'{len(changed)} generator maps, first: {changed[0]}'} if changed else None
        if found:
            differences.append({'scenario': name, **found})
    return differences


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('base', type=Path)
    parser.add_argument('candidate', type=Path)
    parser.add_argument('--json', type=Path, help='also write the verdict as JSON')
    args = parser.parse_args(argv)
    try:
        base, cand = load(args.base), load(args.candidate)
        differences = compare(base, cand)
    except (OSError, ValueError, KeyError) as error:
        print(f'sim-diff: cannot compare: {error}', file=sys.stderr)
        return 2
    verdict = {'equal': not differences, 'scenarios': len(set(base['scenarios']) | set(cand['scenarios'])),
               'differences': differences}
    if args.json:
        args.json.write_text(json.dumps(verdict, indent=2) + '\n')
    if not differences:
        print(f'sim-diff: all {verdict["scenarios"]} scenarios match')
        return 0
    print(f'sim-diff: simulation output differs in {len(differences)} of {verdict["scenarios"]} scenarios')
    for d in differences:
        tick = f' at tick {d["tick"]}' if d['tick'] is not None else ''
        print(f'  {d["scenario"]}{tick}: {d["component"]}')
    return 1


if __name__ == '__main__':
    sys.exit(main())
