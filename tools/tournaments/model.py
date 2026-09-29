"""Versioned experiment/job schema and deterministic planning primitives."""
import itertools
import random
from .common import digest, hash_id, identifier

PROTOCOL_VERSION = 1
SCHEMA_VERSION = 1
DEFAULTS = {'heartbeat_seconds': 15, 'lease_seconds': 300, 'prefetch': 2,
            'infrastructure_attempts': 5, 'process_attempts': 2}


def grid(parameters):
    keys = sorted(parameters)
    return [dict(zip(keys, values)) for values in itertools.product(*(parameters[k] for k in keys))]


def seeded_samples(parameters, count, seed):
    rng = random.Random(seed)
    return [{k: rng.choice(list(parameters[k])) for k in sorted(parameters)} for _ in range(count)]


def rotations(values):
    return [values[i:] + values[:i] for i in range(len(values))]


def job(kind, build, *, inputs=None, seeds=None, config=None, outputs=None,
        limits=None, labels=None, depends_on=None, job_id=None):
    value = {'schema_version': SCHEMA_VERSION, 'type': kind, 'build': build,
             'inputs': inputs or {}, 'seeds': seeds or {}, 'config': config or {},
             'outputs': outputs or {}, 'limits': limits or {}, 'labels': labels or {},
             'depends_on': depends_on or []}
    value['id'] = job_id or digest(value)[:32]
    return value


def validate_job(value):
    from .jobs import get_job_type
    if value.get('schema_version') != SCHEMA_VERSION:
        raise ValueError('unsupported job schema_version')
    identifier(value['id'])
    hash_id(value['build'])
    for key in ('inputs', 'seeds', 'config', 'outputs', 'limits', 'labels'):
        if not isinstance(value[key], dict):
            raise ValueError(f'{key} must be an object')
    for seed in value['seeds'].values():
        if type(seed) is not int or not 0 <= seed <= 2**32 - 1:
            raise ValueError('seeds must be uint32 integers')
    for key in ('timeout_seconds', 'memory_mb', 'estimated_seconds'):
        if key in value['limits'] and (not isinstance(value['limits'][key], (int, float)) or value['limits'][key] <= 0):
            raise ValueError(f'{key} must be positive')
    for dep in value['depends_on']:
        identifier(dep)
    for name, source in value['inputs'].items():
        identifier(name)
        if 'artifact' in source and 'job' in source:
            identifier(source['job'])
            if source['job'] not in value['depends_on']:
                raise ValueError('input dependency must appear in depends_on')
        elif 'sha256' in source:
            hash_id(source['sha256'])
        else:
            raise ValueError('inputs need an artifact identity or job/artifact reference')
    get_job_type(value['type']).validate(value)
    return value


def validate_experiment(manifest):
    if manifest.get('schema_version') != SCHEMA_VERSION:
        raise ValueError('unsupported experiment schema_version')
    identifier(manifest['id'])
    jobs = manifest['jobs']
    if not isinstance(jobs, list) or not jobs:
        raise ValueError('experiment needs at least one job')
    index = {}
    for value in jobs:
        validate_job(value)
        if value['id'] in index:
            raise ValueError('duplicate job id: ' + value['id'])
        index[value['id']] = value
    visiting, visited = set(), set()
    def visit(key):
        if key in visiting:
            raise ValueError('dependency cycle')
        if key not in index:
            raise ValueError('unknown dependency: ' + key)
        if key in visited:
            return
        visiting.add(key)
        for dep in index[key]['depends_on']:
            visit(dep)
        visiting.remove(key)
        visited.add(key)
    for key in index:
        visit(key)
    settings = DEFAULTS | manifest.get('settings', {})
    if settings['heartbeat_seconds'] <= 0 or settings['lease_seconds'] < 2 * settings['heartbeat_seconds']:
        raise ValueError('lease must span at least two positive heartbeat intervals')
    if type(settings['prefetch']) is not int or settings['prefetch'] < 0:
        raise ValueError('prefetch must be a nonnegative integer')
    for key in ('infrastructure_attempts', 'process_attempts', 'input_transfer_slots', 'result_transfer_slots'):
        if type(settings.get(key, 8)) is not int or settings.get(key, 8) < 1:
            raise ValueError(f'{key} must be a positive integer')
    if settings.get('poll_seconds', 1) <= 0:
        raise ValueError('poll_seconds must be positive')
    return manifest
