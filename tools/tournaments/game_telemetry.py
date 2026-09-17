"""Streaming readers for engine gameplay, AI and performance diagnostic records.

Transport keeps stdout verbatim. This layer adds typed, offline analysis without
putting large timelines in result.json or pooling unrelated players/hosts.
"""
from collections import Counter
import csv
import json
import math
from pathlib import Path
import re

PREFIXES = {'GLOB2_MEASURE': 'gameplay', 'GLOB2_MEASURE_HISTORY': 'gameplay',
            # Per-team live state at the 512-tick boundary. Unlike the gameplay
            # measurements these come straight off TeamStat, so they describe
            # state the simulation itself reads and a model fitted to them can be
            # evaluated in-engine. GLOB2_FINAL is deliberately absent: it carries
            # a bare `bld:` token that is not key=value and would only ever parse
            # as an error.
            'GLOB2_ECON': 'team_state', 'GLOB2_TL': 'team_state',
            'GLOB2_TIMELINE': 'team_state'}
FAMILIES = ('gameplay', 'ai', 'performance', 'team_state')
KEY = re.compile(r'([^\s=]+)=')
INTEGER = re.compile(r'[+-]?\d+\Z')
REAL = re.compile(r'[+-]?(?:\d+\.?\d*|\.\d+)(?:[eE][+-]?\d+)?\Z')
DECODER = json.JSONDecoder()
IDENTITY = ('team', 'player', 'ai', 'implementation', 'generation', 'schema',
            'coverage_start', 'tick', 'tick_start', 'session', 'scope', 'mode', 'thread')


def family(prefix):
    if prefix.startswith('GLOB2_AI_'):
        return 'ai'
    if prefix.startswith('GLOB2_PERF_'):
        return 'performance'
    return PREFIXES.get(prefix)


def parse(line):
    """Decode JSON-escaped strings and exact integers, rejecting partial fields."""
    prefix, _, rest = line.strip().partition(' ')
    if not family(prefix):
        return None
    fields = {}
    while rest:
        match = KEY.match(rest)
        if not match:
            raise ValueError('expected key=value')
        key = match[1]
        rest = rest[match.end():]
        if key in fields or not rest:
            raise ValueError('duplicate key or missing value')
        if rest.startswith('"'):
            value, end = DECODER.raw_decode(rest)
            if end < len(rest) and not rest[end].isspace():
                raise ValueError('missing field separator')
            rest = rest[end:].lstrip()
        else:
            token, _, rest = rest.partition(' ')
            rest = rest.lstrip()
            if token == 'na':
                value = None
            elif INTEGER.fullmatch(token):
                value = int(token)
            elif REAL.fullmatch(token):
                value = float(token)
                if not math.isfinite(value):
                    raise ValueError('nonfinite number')
            else:
                value = token
        fields[key] = value
    if not fields:
        raise ValueError('empty telemetry record')
    return prefix, fields


def records(source, record):
    """Yield in log order, with job/attempt/build/host identity and diagnostics.

    Missing logs are unavailable, never zero. Bad rows remain explicit errors;
    valid rows after them are still usable. Artifact hash failures propagate.
    """
    job = record['job']
    context = {'job_id': job['id'], 'attempt_id': record['id'], 'build': job['build'],
               'host': record['host'], 'category': record['category']}
    if not any(a['path'] == 'stdout.log' for a in record['artifacts']):
        yield dict(context, error='missing stdout.log')
        return
    with source.open_artifact(record, 'stdout.log') as stream:
        for line_number, line in enumerate(stream, 1):
            try:
                parsed = parse(line)
            except ValueError as error:
                yield dict(context, line=line_number, error=str(error), raw=line.rstrip('\n'))
                continue
            if parsed:
                prefix, fields = parsed
                yield dict(context, line=line_number, family=family(prefix), record=prefix, values=fields)


def export(source, committed, output):
    """Bounded-memory JSONL + long-form CSV; no lossy cross-game aggregation."""
    out = Path(output)
    counts = Counter()
    jobs = []
    columns = ['job_id', 'attempt_id', 'build', 'host', 'category', 'line', 'family', 'record',
               *IDENTITY, 'field', 'value_type', 'value']
    with (out / 'game-telemetry.jsonl').open('w') as stream, \
            (out / 'game-telemetry-values.csv').open('w', newline='') as table:
        writer = csv.DictWriter(table, columns)
        writer.writeheader()
        for record in committed:
            if record['job']['type'] != 'game':
                continue
            seen = Counter()
            finals = Counter()
            errors = 0
            for row in records(source, record):
                stream.write(json.dumps(row, ensure_ascii=True, allow_nan=False) + '\n')
                if 'error' in row:
                    errors += 1
                    continue
                counts[row['record']] += 1
                seen[row['family']] += 1
                values = row['values']
                if row['record'].endswith('_FINAL') or (row['record'] == 'GLOB2_MEASURE' and values.get('final') == 1):
                    finals[row['family']] += 1
                context = {k: v for k, v in row.items() if k != 'values'}
                context.update({k: values[k] for k in IDENTITY if k in values})
                for key, value in values.items():
                    writer.writerow(dict(context, field=key, value=value,
                                         value_type='unavailable' if value is None else
                                         'integer' if type(value) is int else
                                         'real' if type(value) is float else 'string'))
            jobs.append({'job_id': record['job']['id'], 'attempt_id': record['id'],
                         'host': record['host'], 'build': record['job']['build'],
                         'requested': 'team-timeline' in record['job']['outputs'].get('telemetry', []),
                         'records': dict(seen), 'final_records': dict(finals), 'errors': errors,
                         'unavailable': [f for f in FAMILIES if not seen[f]],
                         'missing_final': [f for f in seen if not finals[f]]})
    return {'schema_version': 1, 'records': dict(counts), 'jobs': jobs,
            'jsonl': 'game-telemetry.jsonl', 'csv': 'game-telemetry-values.csv'}
