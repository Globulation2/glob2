#!/usr/bin/env python3
"""Reproducible fractal-map pilot manifests; execution stays in tools.tournaments.

python3 -m tools.fractal_maps.plan --bundle artifacts/fractal-maps/bundles/ID --output DIR
Catalog IDs and every selectable AI are read from the immutable supplied bundle.
"""
import argparse
import json
from pathlib import Path
from tools.tournaments.bundles import inspect_bundle
from tools.tournaments.experiments import Planner
from tools.tournaments.common import atomic_json
from tools.tournaments.model import validate_experiment


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--bundle', required=True)
    parser.add_argument('--output', required=True)
    args = parser.parse_args()
    bundle = inspect_bundle(args.bundle)
    catalog = bundle['capabilities']
    methods = [g['method'] for g in catalog['generators']
               if g['id'] in ('sierpinski-gardens', 'hilbert-river')]
    if len(methods) != 2:
        raise ValueError('bundle must contain both fractal generators')
    ais = [a['name'] for a in catalog['ais'] if a['id'] != 0]
    root = Path(args.output)
    root.mkdir(parents=True, exist_ok=True)
    outputs = {'map': False}
    for name, seeds in [('training', list(range(20001, 20017))), ('held-out', list(range(30001, 30009)))]:
        config = dict(id=f'fractal-{name}-geometry', generators=methods, map_seeds=seeds,
                      generator_params={'width': 8, 'height': 8, 'teams': 4},
                      grid={'width': [7, 8, 9], 'height': [7, 8, 9], 'teams': list(range(2, 9))},
                      candidates=0, timeout_seconds=120, outputs=outputs)
        atomic_json(root / f'{name}-geometry.config.json', config)
        atomic_json(root / f'{name}-geometry.json', Planner('generator_stress', config, [bundle]).plan())
    # Boundary cases are full requests. The tournament stress planner's grid does not
    # merge missing baseline fields, so materialize every assignment before planning.
    boundary_jobs = {}
    boundary_configs = []
    for generator in catalog['generators']:
        if generator['method'] not in methods:
            continue
        base = {c['id']: c['default'] for c in generator['controls']}
        base.update(width=8, height=8, teams=4)
        variants = [base]
        for control in generator['controls']:
            if control['id'] in ('width', 'height', 'teams'):
                continue
            for value in (control['min'], control['max']):
                variants.append(base | {control['id']: value})
        for amount in (0, 100, 300):
            for workers in (1, 8):
                variants.append(base | {c['id']: amount for c in generator['controls'] if c['id'].endswith('-amount')} | {'workers': workers})
        terrain = [c for c in generator['controls'] if c['id'] not in ('width', 'height', 'teams', 'workers') and not c['id'].endswith('-amount')]
        for edge in ('min', 'max'):
            for width, height, teams in ((7, 7, 2), (7, 9, 8), (9, 7, 8), (9, 9, 8), (8, 8, 4)):
                variants.append(base | {c['id']: c[edge] for c in terrain} | dict(width=width, height=height, teams=teams))
        for index, params in enumerate(variants):
            config = dict(id=f"fractal-boundary-{generator['method']}-{index}", generators=[generator['method']],
                          map_seeds=[20001, 20002, 30001, 30002], generator_params=params,
                          candidates=0, timeout_seconds=120, outputs={'map': False})
            boundary_configs.append(config)
            for value in Planner('generator_stress', config, [bundle]).plan()['jobs']:
                boundary_jobs[value['id']] = value
    atomic_json(root / 'boundaries.json', validate_experiment(dict(schema_version=1, id='fractal-boundaries',
                kind='generator_stress', jobs=list(boundary_jobs.values()), design={'cohorts': boundary_configs})))
    for name, seed, colonies, tested in [('initial', 20001, 2, ais),
                                        ('four-colony', 20002, 4, [a for a in ais if a.lower() in ('nicowar', 'maxima')]),
                                        ('held-out', 30001, 2, ais)]:
        jobs = {}
        configurations = []
        for ai in tested:
            config = dict(id=f'fractal-{name}-{ai.lower()}', generators=methods,
                          map_seeds=[seed], game_seeds=[19], colonies=colonies, ai=ai,
                          generator_params={'width': 8, 'height': 8}, candidates=0,
                          ticks=90000, timeout_seconds=5400,
                          outputs={'replay': True, 'saves': ['initial', 'final'],
                                   'telemetry': ['team-timeline']})
            configurations.append(config)
            for job in Planner('fairness', config, [bundle]).plan()['jobs']:
                jobs[job['id']] = job
        manifest = validate_experiment(dict(schema_version=1, id=f'fractal-{name}', kind='fairness',
                                            jobs=list(jobs.values()), design={'cohorts': configurations}))
        atomic_json(root / f'{name}-games.json', manifest)
    atomic_json(root / 'coverage.json', {'bundle': bundle['id'], 'generators': methods, 'ais': ais,
                                       'engine_seed': 19, 'ticks': 90000})
    print(json.dumps({'output': str(root), 'ais': ais, 'generators': methods}))


if __name__ == '__main__':
    main()
