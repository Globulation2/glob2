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
if __package__:
    from .tournaments.resources import PROFILES, classify_cli_capability
    from .tournaments.cli import positive_integer
else:
    from tournaments.resources import PROFILES, classify_cli_capability
    from tournaments.cli import positive_integer


def resource_request(execution_profile=None, memory_planning_budget_bytes=None):
    """Optional owner-only map intent; omission preserves older native calls."""
    if execution_profile is not None and (type(execution_profile) is not str or execution_profile not in PROFILES):
        raise ValueError('Invalid execution profile')
    if memory_planning_budget_bytes is not None:
        memory_planning_budget_bytes = positive_integer(memory_planning_budget_bytes)
    if execution_profile is None and memory_planning_budget_bytes is None:
        return None
    return {'execution_profile': execution_profile,
            'memory_planning_budget_bytes': memory_planning_budget_bytes}


def native_resource_options(binary, command, request, timeout):
    """Probe only explicit requests; unsupported map options never disappear."""
    if request is None:
        return []
    result = subprocess.run([str(binary), 'help', '--format', 'json'], capture_output=True,
                            text=True, timeout=timeout, check=True)
    description = json.loads(result.stdout)
    if classify_cli_capability(description) != 'execution_profiles_v1':
        raise ValueError('Binary does not advertise the execution resource contract')
    commands = [c for c in description['commands'] if isinstance(c, dict)
                and c.get('path') == command and c.get('available') is True]
    if len(commands) != 1:
        raise ValueError('Binary does not advertise ' + command)
    names = {o.get('name') for o in commands[0].get('options', []) if isinstance(o, dict)}
    flags = []
    for key, flag in [('execution_profile', '--execution-profile'),
                      ('memory_planning_budget_bytes', '--memory-budget-bytes')]:
        if request[key] is not None:
            if flag not in names:
                raise ValueError('Binary does not advertise ' + command + ' ' + flag)
            flags += [flag, str(request[key])]
    return flags


def validate_resource_report(report, request):
    """Validate requested map provenance, without claiming RSS or tier tuning."""
    plan = report.get('execution_resources')
    if request is None:
        return plan  # Preserve legacy reports with no resource contract.
    if not isinstance(plan, dict) or type(plan.get('schema_version')) is not int or \
            plan['schema_version'] != 1 or type(plan.get('policy_version')) is not int or plan['policy_version'] != 1:
        raise ValueError('Missing or unsupported map execution resource plan')
    resources = plan.get('resolved_resources')
    if not isinstance(resources, dict):
        raise ValueError('Missing map resolved resources')
    if type(resources.get('compute_participants')) is not int or resources['compute_participants'] != 1 or \
            resources.get('compute_participants_include_owner') is not True:
        raise ValueError('Map resource plan must retain its one owner participant')
    profile = request['execution_profile']
    if profile is not None and (plan.get('requested_execution_profile') != profile or
            plan.get('selected_execution_profile') != profile or plan.get('profile_source') != 'explicit_profile'):
        raise ValueError('Map execution profile differs from explicit request')
    memory = request['memory_planning_budget_bytes']
    if memory is not None and (type(resources.get('planning_memory_budget_bytes')) is not int or
                              resources['planning_memory_budget_bytes'] != memory):
        raise ValueError('Map planning memory differs from explicit request')
    if memory is not None:
        original_memory = plan.get('original_request', {}).get('resource_overrides', {}).get('planning_memory_budget_bytes')
        if plan.get('resource_sources', {}).get('planning_memory_budget_bytes') != 'explicit_override' or \
                type(original_memory) is not int or original_memory != memory:
            raise ValueError('Map planning memory lacks explicit request provenance')
    return plan


def load_catalog(binary, timeout=60, generator_packages=()):
    result = subprocess.run([str(binary), 'info', 'catalog', '--format', 'json']+[v for p in generator_packages for v in ['--generator-package',str(p)]], capture_output=True,
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


def generate_map(binary, method, seed, width, height, teams, settings=None, timeout=120, generator_packages=(), *,
                 execution_profile=None, memory_planning_budget_bytes=None):
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
    request = resource_request(execution_profile, memory_planning_budget_bytes)
    resource_options = native_resource_options(binary, 'map study', request, timeout)
    started = time.monotonic()
    row = dict(category='execution_error', detail='', returncode=None, native=None)
    if request is not None:
        row['execution_resources'] = {'requested': request,
            'scope': 'owner-only map policy; planning memory is not an RSS limit'}
    with tempfile.TemporaryDirectory(prefix='glob2-map-study-') as directory:
        command = [str(binary), 'map', 'study', str(method),
                   '--seed', str(seed), '--output-dir', directory]
        command += [v for p in generator_packages for v in ['--generator-package',str(p)]]
        command += resource_options
        for key, value in params.items():
            command += ['--set', f'{key}={value}']
        try:
            result = subprocess.run(command, capture_output=True, text=True, timeout=timeout)
            row['returncode'] = result.returncode
            native = json.loads((Path(directory) / 'result.json').read_text())
            if native.get('schema_version') != 1:
                raise ValueError('Unsupported native result schema')
            row['native'] = native
            if request is not None:
                row['execution_resources']['resolved'] = validate_resource_report(native, request)
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
