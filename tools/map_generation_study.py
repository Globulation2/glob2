#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Structured native map requests shared by local generator studies.

Resolve generators from one catalog per study, then submit independent requests. Refusals,
process failures, timeouts and malformed artifacts remain distinct. No stdout text is a schema.
The native command isolates its profile beneath the temporary output directory.
"""
import json
import math
import subprocess
import tempfile
import time
from pathlib import Path


def load_catalog(binary, timeout=60):
    result = subprocess.run([str(binary), '--headless-catalog'], capture_output=True,
                            text=True, timeout=timeout, check=True)
    catalog = json.loads(result.stdout)
    if catalog.get('schema_version') != 1 or not isinstance(catalog.get('generators'), list):
        raise ValueError('Unsupported generator catalog schema')
    return catalog


def generator_definition(catalog, name):
    for definition in catalog['generators']:
        if str(name) in (definition['id'], str(definition['method'])):
            return definition
    raise ValueError(f'Unknown generator: {name}')


def dimension_exponent(tiles):
    if not isinstance(tiles, int) or tiles < 1 or tiles & (tiles - 1):
        raise ValueError('Map dimensions must be positive powers of two')
    return tiles.bit_length() - 1


def generate_map(binary, method, seed, width, height, teams, settings=None, timeout=120):
    """Return category (completed/refused/execution_error), detail, timing and native JSON.

    This is a single seed, without lobby candidate selection. Dimensions are tile counts;
    conversion to the native API's exponents happens only here. Caller configuration errors
    raise; engine-side invalid requests are retained as refusals with their native status.
    """
    params = dict(width=dimension_exponent(width), height=dimension_exponent(height), teams=teams)
    if set(settings or {}) & {'width', 'height', 'teams'}:
        raise ValueError('Dimensions and teams must be supplied as request arguments')
    params.update(settings or {})
    if timeout <= 0:
        raise ValueError('Timeout must be positive')
    started = time.monotonic()
    row = dict(category='execution_error', detail='', returncode=None, native=None)
    with tempfile.TemporaryDirectory(prefix='glob2-map-study-') as directory:
        command = [str(binary), '--generate-map', '--generator', str(method),
                   '--map-seed', str(seed), '--output-dir', directory]
        for key, value in params.items():
            command += ['--param', f'{key}={value}']
        try:
            result = subprocess.run(command, capture_output=True, text=True, timeout=timeout)
            row['returncode'] = result.returncode
            native = json.loads((Path(directory) / 'result.json').read_text())
            if native.get('schema_version') != 1:
                raise ValueError('Unsupported native result schema')
            row['native'] = native
            status = native.get('status')
            if status == 'completed' and result.returncode == 0:
                report = native.get('map_report')
                if (not isinstance(report, dict) or
                        report.get('generation', {}).get('outcome', {}).get('success') is not True):
                    raise ValueError('Completed result has no successful map report')
                validate_map_report(report)
                row['category'] = 'completed'
            elif (status, result.returncode) in {('generation_failed', 4), ('invalid_request', 2)}:
                row['category'] = 'refused'
            row['detail'] = native.get('diagnostic', '')
            if row['category'] == 'execution_error':
                row['detail'] = row['detail'] or f'Native status {status!r}, exit {result.returncode}'
        except subprocess.TimeoutExpired:
            row['detail'] = f'Timed out after {timeout:g} seconds'
        except (OSError, ValueError, TypeError, AttributeError, KeyError) as error:
            row['detail'] = f'Execution/result error: {error}'
    row['seconds'] = round(time.monotonic() - started, 3)
    return row


def finite_number(value):
    if isinstance(value, bool) or not isinstance(value, (int, float)) or not math.isfinite(value):
        raise ValueError('Expected a finite numeric map measurement')
    return value


def validate_map_report(report):
    """Validate the fields consumed by all local studies before classifying a result as complete."""
    for name in ('grass', 'sand', 'water'):
        value = finite_number(report['terrain'][name]['percent'])
        if not 0 <= value <= 100:
            raise ValueError('Terrain percentage is out of range')
    for name in ('wheat', 'wood', 'stone', 'algae', 'cherry', 'orange', 'prune'):
        count = finite_number(report['resources']['types'][name]['coverage']['tiles'])
        if count < 0:
            raise ValueError('Negative resource coverage')
    if finite_number(report['space']['build_sites_4x4']) < 0:
        raise ValueError('Negative building capacity')
    if finite_number(report['space']['land_regions']['components']) < 0:
        raise ValueError('Negative component count')
    finite_number(report['fertility']['all_tiles']['mean'])
    quality = report['generation'].get('selection_quality') or {}
    for key in ('score', 'worst_fitness'):
        if quality.get(key) is not None:
            finite_number(quality[key])
    report_metrics(report)


def report_metrics(report):
    """Keep compatibility means plus extrema; retain raw subject-labelled telemetry separately."""
    measurements = {}
    records = (report.get('generation', {}).get('telemetry') or {}).get('records', [])
    for record in records:
        value = record.get('value')
        if isinstance(value, (int, float)) and not isinstance(value, bool):
            measurements.setdefault(record['key'], []).append(finite_number(value))
    metrics = {}
    for key, values in measurements.items():
        metrics['tel:' + key] = sum(values) / len(values)
        metrics['tel-min:' + key] = min(values)
        metrics['tel-max:' + key] = max(values)
    if report.get('generation', {}).get('outcome', {}).get('success'):
        for key, value in report['terrain'].items():
            if isinstance(value, dict):
                metrics['terrain%:' + key] = value['percent']
        for key, value in report['resources']['types'].items():
            metrics['tiles:' + key] = value['coverage']['tiles']
        metrics['sites4x4'] = report['space']['build_sites_4x4']
        metrics['fertility'] = report['fertility']['all_tiles']['mean']
    return metrics, records
