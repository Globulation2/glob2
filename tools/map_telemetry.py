#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Collect generation-only reports; summarize OUT into records.jsonl and summary.json.

Each collect run requires a new directory. Raw reports, logs and per-attempt manifests
are retained even on failure. Summary metrics are record-weighted; fallback rates count
attempts, never repeated events. Missing measurements are absent, not zero.
"""
if __package__:
    from .build_paths import native_binary
else:
    from build_paths import native_binary
import argparse
from concurrent.futures import ThreadPoolExecutor
import json
import math
import os
from pathlib import Path
import subprocess
import tempfile
import time

ROOT = Path(__file__).resolve().parents[1]


def write_json(path, value):
    path.write_text(json.dumps(value, indent=2, sort_keys=True, allow_nan=False) + '\n')


def read_report(path):
    report = json.loads(path.read_text(), parse_constant=lambda s: (_ for _ in ()).throw(
        ValueError('Non-finite JSON number: ' + s)))
    if report.get('schema_version') != 2 or report.get('report_type') not in (
            'map', 'generation_failure'):
        raise ValueError('Expected version-2 map or generation_failure report')
    if not isinstance(report.get('generation'), dict):
        raise ValueError('Missing generation object')
    return report


def attempt(binary, generator, seed, settings, directory, timeout):
    directory.mkdir()
    report_path = directory / 'report.json'
    manifest = {'generator': generator, 'seed': seed, 'requested_parameters': settings,
                'report': 'report.json', 'status': 'running', 'returncode': None}
    with tempfile.TemporaryDirectory(prefix='glob2-map-telemetry-') as profile:
        command = [str(binary), '--generate-map', generator, '-d', str(ROOT),
                   '--seed', str(seed), '--json', str(report_path)]
        for key, value in settings.items():
            command += ['--set', key + '=' + value]
        manifest['command'] = command
        write_json(directory / 'manifest.json', manifest)
        env = dict(os.environ, GLOB2_USER_DIR=profile)
        started = time.monotonic()
        with (directory / 'stdout.log').open('wb') as stdout, (
                directory / 'stderr.log').open('wb') as stderr:
            try:
                result = subprocess.run(command, cwd=profile, env=env, stdout=stdout,
                                        stderr=stderr, timeout=timeout, check=False)
                manifest['returncode'] = result.returncode
                manifest['status'] = 'ok' if result.returncode == 0 else 'process_failure'
            except subprocess.TimeoutExpired:
                manifest['status'] = 'timeout'
            except OSError as error:
                manifest.update(status='launch_failure', process_error=str(error))
        manifest['seconds'] = time.monotonic() - started
    try:
        report = read_report(report_path)
        generation = report['generation']
        manifest['report_type'] = report['report_type']
        manifest['outcome'] = generation.get('outcome')
        if generation.get('generator') != generator or generation.get('seed') != seed:
            raise ValueError('Report generator/seed differs from requested attempt')
        if manifest['status'] == 'ok' and (report['report_type'] != 'map' or
                not (generation.get('outcome') or {}).get('success')):
            manifest['status'] = 'generation_failure'
    except (OSError, ValueError, TypeError, AttributeError) as error:
        manifest['report_error'] = str(error)
        if manifest['status'] == 'ok':
            manifest['status'] = 'report_failure'
    write_json(directory / 'manifest.json', manifest)
    return manifest


def summarize(directory):
    directory = Path(directory).resolve()
    groups = {}
    with (directory / 'records.jsonl').open('w') as flattened:
        for path in sorted(directory.glob('attempt-*/manifest.json')):
            manifest = json.loads(path.read_text())
            generation = {}
            try:
                report = read_report(path.parent / 'report.json')
                candidate = report['generation']
                if (candidate.get('generator'), candidate.get('seed')) == (
                        manifest['generator'], manifest['seed']):
                    generation = candidate
            except (OSError, ValueError, TypeError, AttributeError):
                pass
            identity = {'generator': manifest['generator'], 'revision': generation.get('revision'),
                        'parameters': generation.get('parameters', manifest['requested_parameters']),
                        'parameters_source': 'report' if 'parameters' in generation else 'request'}
            group_key = json.dumps(identity, sort_keys=True, separators=(',', ':'))
            group = groups.setdefault(group_key, dict(identity, attempts=0, statuses={},
                telemetry_attempts=0, incomplete_telemetry_attempts=0, fallbacks={}, metrics={}))
            group['attempts'] += 1
            status = manifest['status']
            group['statuses'][status] = group['statuses'].get(status, 0) + 1
            telemetry = generation.get('telemetry') or {}
            records = telemetry.get('records', [])
            if telemetry.get('schema_version') != 1 or not telemetry.get('enabled'):
                continue
            group['telemetry_attempts'] += 1
            if telemetry.get('dropped_records', 0) or telemetry.get('invalid_values', 0):
                group['incomplete_telemetry_attempts'] += 1
            fallbacks = set()
            for sequence, record in enumerate(records):
                row = dict(identity, attempt=path.parent.name, seed=manifest['seed'],
                           status=status, sequence=sequence, report=str(path.parent / 'report.json'),
                           **record)
                flattened.write(json.dumps(row, sort_keys=True, allow_nan=False) + '\n')
                key, value = record['key'], record['value']
                if record['kind'] == 'fallback':
                    fallbacks.add(key)
                if record['kind'] == 'measurement' and type(value) in (int, float) and math.isfinite(value):
                    group['metrics'].setdefault(key, []).append(value)
            for key in fallbacks:
                group['fallbacks'][key] = group['fallbacks'].get(key, 0) + 1
    for group in groups.values():
        group['fallbacks'] = {key: {'attempts': count, 'denominator_attempts': group['attempts'],
            'observed_rate': count / group['attempts']} for key, count in group['fallbacks'].items()}
        group['metrics'] = {key: {'records': len(values), 'minimum': min(values),
            'maximum': max(values), 'mean': sum(values) / len(values)}
            for key, values in group['metrics'].items()}
    result = {'groups': list(groups.values()), 'notes': [
        'Fallback rates count attempts; absent or truncated telemetry can undercount events.',
        'Unknown revisions/parameters remain separate groups; do not infer identity for missing reports.',
        'Numeric summaries weight records, not maps. Boolean and choice values remain in records.jsonl.',
        'Missing metrics are not zero; sequence and subject identify repeated observations.']}
    write_json(directory / 'summary.json', result)
    return result


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest='command', required=True)
    collect = commands.add_parser('collect')
    collect.add_argument('--generators', required=True, help='Comma-separated stable generator IDs')
    collect.add_argument('--seed-start', type=int, default=1)
    collect.add_argument('--count', type=int, default=8)
    collect.add_argument('--set', action='append', default=[], dest='settings')
    collect.add_argument('--out', type=Path, required=True)
    collect.add_argument('--jobs', type=int, default=3)
    collect.add_argument('--timeout', type=float, default=60)
    collect.add_argument('--binary', type=Path, default=native_binary())
    summary = commands.add_parser('summarize')
    summary.add_argument('out', type=Path)
    args = parser.parse_args(argv)
    if args.command == 'summarize':
        summarize(args.out)
        return 0
    generators = args.generators.split(',')
    if not generators or any(not g or g.startswith('-') for g in generators):
        parser.error('Supply nonempty generator IDs')
    if not 1 <= args.jobs <= 64 or not 1 <= args.count <= 100000:
        parser.error('jobs must be 1..64 and count 1..100000')
    if not math.isfinite(args.timeout) or args.timeout <= 0:
        parser.error('timeout must be finite and positive')
    if args.seed_start < 0 or args.seed_start + args.count - 1 > 0xffffffff:
        parser.error('Seeds must fit uint32')
    settings = {}
    for setting in args.settings:
        key, separator, value = setting.partition('=')
        if not separator or not key or not value or key == 'seed' or key in settings:
            parser.error('Each --set needs a unique key=value; set seeds with --seed-start')
        settings[key] = value
    out = args.out.resolve()
    out.mkdir(parents=True, exist_ok=False)
    binary = args.binary.resolve()
    write_json(out / 'run.json', {'generators': generators, 'seed_start': args.seed_start,
        'count': args.count, 'parameters': settings, 'binary': str(binary),
        'jobs': args.jobs, 'timeout': args.timeout})
    tasks = [(g, s) for g in generators for s in range(args.seed_start, args.seed_start + args.count)]
    def run(item):
        index, (generator, seed) = item
        return attempt(binary, generator, seed, settings, out / f'attempt-{index:06d}', args.timeout)
    with ThreadPoolExecutor(max_workers=args.jobs) as pool:
        results = list(pool.map(run, enumerate(tasks)))
    summarize(out)
    failed = sum(result['status'] != 'ok' for result in results)
    print(f'{len(results)} attempts; {failed} failed; results: {out}')
    return 1 if failed else 0


if __name__ == '__main__':
    raise SystemExit(main())
