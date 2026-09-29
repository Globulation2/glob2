#!/usr/bin/env python3
"""Triage bulk generation results without re-running maps or stepping the engine.

The hard checks mirror claims that the finished map report can directly support.
Soft flags select previews for human inspection; a low generic quality score alone
is not a generator failure. Missing jobs and invalid requests stay separate from
accepted-configuration generation failures.
"""
import argparse
from collections import Counter, defaultdict
import csv
import json
from pathlib import Path

from tools.tournaments.results import Results


def inspect(paths, output):
    categories, hard, soft, rows = Counter(), Counter(), Counter(), []
    missing = []
    configurations = defaultdict(lambda: defaultdict(set))
    repeated_identities = Counter()
    for directory in paths:
        source = Results(directory)
        seen = set()
        catalogs = {}
        for path in (source.root / 'builds').glob('*/bundle.json'):
            bundle = json.loads(path.read_text())
            catalogs[path.parent.name] = {
                generator['method']: generator
                for generator in bundle['capabilities']['generators']
            }
        for record in source:
            job = record['job']
            if job['id'] in seen:
                raise ValueError(f"duplicate committed job in {directory}: {job['id']}")
            seen.add(job['id'])
            repeated_identities[job['id']] += 1
            category = record['category']
            categories[category] += 1
            # The stress grid may omit control defaults, while the boundary
            # matrix explicitly materializes them. Normalize before deciding
            # whether different seeds changed the outcome of one configuration.
            generator = job['labels']['generator']
            definition = catalogs.get(job['build'], {}).get(generator, {})
            params = {control['id']: control['default']
                      for control in definition.get('controls', [])}
            params.update(job['config']['params'])
            seed = job['seeds']['map']
            key = (generator, json.dumps(params, sort_keys=True))
            configurations[key][category].add(seed)
            result = record.get('result') or {}
            report = result.get('map_report') or {}
            findings = []
            if category == 'success':
                if not report:
                    findings.append(('hard', 'missing-finished-report'))
                else:
                    outcome = report.get('generation', {}).get('outcome', {})
                    if outcome.get('success') is not True:
                        findings.append(('hard', 'report-says-generation-failed'))
                    space = report.get('space', {})
                    movement = report.get('movement', {}).get('walking', {})
                    colonies = movement.get('colonies', [])
                    teams = int(params['teams'])
                    if len(colonies) != teams:
                        findings.append(('hard', 'wrong-colony-count'))
                    if space.get('land_regions', {}).get('components') != 1:
                        findings.append(('hard', 'split-walking-land'))
                    routes = movement.get('between_colonies', [])
                    if len(routes) != teams or any(
                        len(line) != teams or any(value is None for value in line)
                        for line in routes
                    ):
                        findings.append(('hard', 'missing-walking-route'))
                    # A 24x5 service apron is protected per home. Spare modules
                    # may increase this count; no-growth flags are serialized.
                    if space.get('growth_disabled', {}).get('tiles', 0) < 120 * teams:
                        findings.append(('hard', 'missing-growth-protection'))
                    for colony in colonies:
                        resources = colony.get('resources', {})
                        for crop in ('wheat', 'wood', 'stone'):
                            if resources.get(crop, {}).get('reachable_deposit_tiles', 0) <= 0:
                                findings.append(('hard', f'unreachable-{crop}'))
                        if colony.get('catchment_build_sites_4x4', 0) < 10:
                            findings.append(('hard', 'no-expansion-room'))
                        for crop in ('wheat', 'wood', 'stone'):
                            cost = resources.get(crop, {}).get('nearest_gather_cost')
                            if cost is not None and cost > 50:
                                findings.append(('soft', f'long-{crop}-haul'))
                    quality = report.get('canonical_quality', {})
                    if quality.get('fairness', 1) < 0.4:
                        findings.append(('soft', 'low-quality-fairness'))
                    if quality.get('worst', 1) < 0.2:
                        findings.append(('soft', 'low-quality-score'))
                    if report.get('terrain', {}).get('water', {}).get('percent', 0) > 65:
                        findings.append(('soft', 'very-wet'))
            else:
                # The category itself is the failure. An invalid request is an
                # explicit geometric rejection, not a crash or missing report.
                if category not in ('invalid_request',):
                    findings.append(('hard', f'job-{category}'))
            for severity, reason in findings:
                (hard if severity == 'hard' else soft)[reason] += 1
            rows.append(dict(study=directory.name, job=job['id'], generator=generator,
                             seed=seed, width=params['width'], height=params['height'],
                             teams=params['teams'], category=category,
                             hard=';'.join(reason for sev, reason in findings if sev == 'hard'),
                             soft=';'.join(reason for sev, reason in findings if sev == 'soft'),
                             diagnostic=(result.get('diagnostic') or '').strip()))
        missing.extend(dict(study=directory.name, job=job['id'])
                       for job in source.manifest['jobs'] if job['id'] not in seen)
    output.mkdir(parents=True, exist_ok=True)
    with (output / 'jobs.csv').open('w', newline='') as stream:
        writer = csv.DictWriter(stream, rows[0].keys() if rows else
                                ['study', 'job', 'generator', 'seed', 'width', 'height',
                                 'teams', 'category', 'hard', 'soft', 'diagnostic'])
        writer.writeheader()
        writer.writerows(rows)
    mixed = [dict(generator=key[0], params=json.loads(key[1]),
                  categories={name: sorted(seeds) for name, seeds in states.items()})
             for key, states in configurations.items()
             if len(states) > 1]
    summary = dict(planned=sum(len(Results(path).manifest['jobs']) for path in paths),
                   observed=len(rows), categories=dict(categories), hard=dict(hard),
                   soft=dict(soft), missing_jobs=missing, mixed_seed_outcomes=mixed,
                   repeated_job_identities=sum(value > 1
                                               for value in repeated_identities.values()))
    (output / 'summary.json').write_text(json.dumps(summary, indent=2) + '\n')
    print(json.dumps({k: v if k not in ('missing_jobs', 'mixed_seed_outcomes') else len(v)
                      for k, v in summary.items()}, indent=2))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('studies', nargs='+', type=Path)
    parser.add_argument('--output', required=True, type=Path)
    args = parser.parse_args()
    inspect(args.studies, args.output)
