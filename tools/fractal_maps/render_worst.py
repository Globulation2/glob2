#!/usr/bin/env python3
"""Rebuild and preview the bulk study's least comfortable successful maps.

Selections use existing final-map report values. The original immutable bundle
reconstructs each chosen seed and effective controls, so a reviewer can inspect
the actual map rather than infer its shape from one quality number.
"""
import argparse
import json
from pathlib import Path
import subprocess

from tools.tournaments.results import Results


def cases(studies):
    chosen = {}
    for study in studies:
        for record in Results(study):
            if record['category'] != 'success':
                continue
            report = record.get('result', {}).get('map_report') or {}
            if not report:
                continue
            job = record['job']
            generator = job['labels']['generator']
            quality = report.get('canonical_quality', {})
            colonies = report.get('movement', {}).get('walking', {}).get('colonies', [])
            routes = report.get('movement', {}).get('walking', {}).get('between_colonies', [])
            values = {
                'lowest-quality': quality.get('worst'),
                'lowest-fairness': quality.get('fairness'),
                'wettest': report.get('terrain', {}).get('water', {}).get('percent'),
                'longest-walk': max((cost for row in routes for cost in row), default=None),
                'smallest-expansion': min((colony.get('catchment_build_sites_4x4', 0)
                                           for colony in colonies), default=None),
            }
            for criterion, value in values.items():
                if value is None:
                    continue
                key = (generator, criterion)
                best = chosen.get(key)
                better = best is None or (value > best[0] if criterion in
                          ('wettest', 'longest-walk') else value < best[0])
                if best is None or better or (value == best[0] and job['id'] < best[1]['job']['id']):
                    chosen[key] = (value, record)
    return chosen


def render(studies, bundle, output):
    executable = bundle / 'glob2'
    data = bundle / 'data'
    if not executable.is_file() or not data.is_dir():
        raise ValueError('bundle must contain immutable supplied/glob2 and data')
    output.mkdir(parents=True, exist_ok=True)
    selected = []
    for (generator, criterion), (value, record) in sorted(cases(studies).items()):
        job = record['job']
        params = job['config']['params']
        identity = f'{generator}-{criterion}-{job["id"][:10]}'
        root = output / identity
        args = [str(executable), '--generate-map', str(generator),
                '--seed', str(job['seeds']['map']), '--width', str(1 << params['width']),
                '--height', str(1 << params['height']), '--teams', str(params['teams']),
                '--workers', str(params.get('workers', 4)),
                '--output', str(root.with_suffix('.map')),
                '--preview', str(root.with_suffix('.png')),
                '--json', str(root.with_suffix('.json'))]
        for key, number in sorted(params.items()):
            if key not in ('width', 'height', 'teams', 'workers'):
                args.extend(['--set', f'{key}={number}'])
        args.extend(['-d', str(data)])
        outcome = subprocess.run(args, text=True, capture_output=True)
        root.with_suffix('.log').write_text(outcome.stdout + outcome.stderr)
        selected.append(dict(criterion=criterion, value=value, study=record['experiment'],
                             job=job['id'], seed=job['seeds']['map'], generator=generator,
                             params=params, preview=str(root.with_suffix('.png')),
                             status=outcome.returncode))
        if outcome.returncode:
            raise RuntimeError(f'reconstruction failed for {job["id"]}: see {root}.log')
    (output / 'selection.json').write_text(json.dumps(selected, indent=2) + '\n')
    print(json.dumps({'selected': len(selected), 'output': str(output)}))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('studies', nargs='+', type=Path)
    parser.add_argument('--bundle', required=True, type=Path,
                        help='immutable bundle supplied directory')
    parser.add_argument('--output', required=True, type=Path)
    args = parser.parse_args()
    render(args.studies, args.bundle, args.output)
