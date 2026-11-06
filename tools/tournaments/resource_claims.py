"""Resource-claim probe trace reader for tournament games.

A game run with ``resource_claims_probe`` (or ``resource_claims_movement_only``)
writes a compressed JSONL trace: a schema row, sampled live journey rows and a
final summary row (the live footer: claim event, release reason, selection
filter, policy extension and contention counts, decision outcome counters and
the all-transport movement audit). This module validates that framing and
collects per-game footers. It never pools journeys as independent games.
"""
import argparse
import gzip
import io
import json
from pathlib import Path

from .bundles import package_identity
from .common import atomic_json, digest
from .results import Results

ARTIFACT = 'resource-claims-shadow.jsonl.gz'
TRACE_VERSION = 2


def trace_rows(stream):
    """Validate complete stream framing while yielding the schema, rows and summary."""
    schema = json.loads(next(stream, 'null'))
    if not isinstance(schema, dict) or schema.get('kind') != 'schema' or schema.get('version') != TRACE_VERSION:
        raise ValueError('unsupported resource claim schema')
    movement_only = schema.get('movement_only')
    if type(movement_only) is not bool:
        raise ValueError('invalid movement-only mode')
    if schema.get('movement_audit_version') != 1:
        raise ValueError('claim trace requires movement audit version 1')
    from .claim_journeys import Validator
    from .claim_outcomes import Validator as OutcomeValidator
    journeys = Validator(schema)
    outcomes = OutcomeValidator(schema)
    yield schema
    summary = None
    for line in stream:
        row = json.loads(line)
        if not isinstance(row, dict):
            raise ValueError('claim trace row must be an object')
        if summary is not None:
            raise ValueError('rows after claim summary')
        if row.get('kind') == 'summary':
            summary = row
        elif row.get('kind') == 'journey':
            if movement_only:
                raise ValueError('journey in movement-only trace')
            journeys.row(row)
            yield row
        else:
            raise ValueError('unknown claim trace row')
    if not summary or summary.get('complete') is not True:
        raise ValueError('incomplete resource claim trace')
    journeys.finish(summary)
    outcomes.finish(summary)
    if movement_only and (summary['live']['journey_eligible'] or any(summary['live']['event_counts'])):
        raise ValueError('movement-only trace contains claim observation')
    yield summary


def read_game(stream):
    """Per-game live footer, journey totals and movement audit from one trace."""
    rows = trace_rows(stream)
    schema = next(rows)
    summary = None
    journey_digests = set()
    journeys = {'rows': 0, 'steps': 0, 'live_cost': 0, 'partial': 0, 'baseline_missing': 0, 'terminals': {}}
    for row in rows:
        if row['kind'] == 'summary':
            summary = row
            continue
        journey_digests.add(row['policy_digest'])
        journeys['rows'] += 1
        journeys['steps'] += row['steps']; journeys['live_cost'] += row['live_cost']
        journeys['partial'] += row['terminal'] in ('timeout', 'end_of_segment')
        journeys['baseline_missing'] += row['baseline_status'] != 'reached'
        journeys['terminals'][row['terminal']] = journeys['terminals'].get(row['terminal'], 0) + 1
    return {'schema': schema, 'summary': summary, 'live': summary['live'],
            'decision_outcomes': summary['decision_outcomes'], 'movement_audit': summary['movement_audit'],
            'journey_policy_digests': sorted(journey_digests), 'journeys': journeys,
            'journey_interpretation': 'Per-game diagnostic totals; partial journeys retain partial costs; acquisition static baseline is not a dynamic optimum; no independent-unit inference.'}


def analyze(directory):
    """Collect each game's attested live footer; missing or failed games stay explicit."""
    source = Results(directory)
    expected = {j['id']: j for j in source.manifest['jobs'] if j['type'] == 'game'}
    coverage, schemas, live_policies = [], set(), set()
    for record in source:
        game = record['job']; game_id = game['id']
        if game_id not in expected:
            continue
        del expected[game_id]
        entry = {'game': game_id, 'category': record['category']}
        try:
            if record['category'] != 'success':
                raise ValueError('game execution did not succeed')
            # The artifact itself is gzip, independent of transport encoding.
            with source.open_artifact(record, ARTIFACT, 'rb') as packed:
                with gzip.GzipFile(fileobj=packed, mode='rb') as decompressed:
                    with io.TextIOWrapper(decompressed) as stream:
                        measured = read_game(stream)
            resolved = (record.get('result') or {}).get('resolved', {})
            from .claim_journeys import attest_digests
            attest_digests(measured['journey_policy_digests'], resolved)
            expected_policy = game.get('config', {}).get('resource_claims_policy')
            if resolved.get('active_claim_policy') == 'experimental' or expected_policy is not None:
                from .claim_policy import attested_policy_digest, policy_digest
                identity = attested_policy_digest(resolved)
                if expected_policy is not None and identity != policy_digest(expected_policy):
                    raise ValueError('experimental claim policy differs from frozen request')
                live_policies.add(identity)
            schemas.add(digest(measured['schema']))
            entry.update({'measured': True, 'active_claim_policy': resolved.get('active_claim_policy'),
                          'policy_digest': resolved.get('resource_claims_policy_digest'),
                          'live': measured['live'], 'decision_outcomes': measured['decision_outcomes'],
                          'journeys': measured['journeys'], 'movement_audit': measured['movement_audit']})
        except (OSError, EOFError, ValueError, StopIteration, KeyError, TypeError) as error:
            entry.update({'measured': False, 'error': str(error)})
        coverage.append(entry)
    coverage.extend({'game': game_id, 'category': 'missing', 'measured': False} for game_id in expected)
    if len(schemas) > 1 or len(live_policies) > 1:
        raise ValueError('mixed claim trace schemas or policies; analyze frozen cohorts separately')
    return {'schema_version': 2, 'games': coverage, 'measured_games': sum(entry['measured'] for entry in coverage),
            'expected_games': len(coverage), 'tool_package_identity': package_identity(),
            'interpretation': 'One entry per game; cluster comparisons by game and paired seed, never by journey or decision.'}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('directory')
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    if args.output.exists():
        parser.error('output already exists; retain frozen reports')
    atomic_json(args.output, analyze(args.directory))


if __name__ == '__main__':
    main()
