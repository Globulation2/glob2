"""Bounded, paired live claim experiments. Planning only; never dispatches games."""
import argparse
import copy
import json
import shutil
from pathlib import Path

from .claim_policy import INTERVALS, policy_digest, validate_policy
from .common import digest
from .model import job, validate_experiment

GiB = 1024**3


def factor_configs(control, patterns=()):
    """One factor at a time, including the explicitly coupled refresh factor."""
    validate_policy(control)
    rows = []
    seen = set()

    def add(label, changes):
        policy = dict(control, **changes)
        validate_policy(policy)
        key = policy_digest(policy)
        if key not in seen:
            rows.append({'label': label, 'policy': policy})
            seen.add(key)

    add('control', {})
    for interval in sorted(INTERVALS):
        for factor in ('destination_refresh_j', 'route_refresh_j'):
            add(f'{factor}-{interval}', {factor: interval})
        add(f'coupled-refresh-{interval}', {'destination_refresh_j': interval, 'route_refresh_j': interval})
    for factor, values in (
        ('fallback', ('wait', 'roam', 'release_job', 'bounded_wait_release')),
        ('pending_step', ('wait', 'direct_heading')),
        ('route_target', ('claimed_access', 'probe_then_gradient')),
        ('use_primary_gradient', (False, True)),
        ('selection', ('first_available', 'closest_geometric')),
    ):
        for value in values:
            add(f'{factor}-{str(value).lower()}', {factor: value})
    for asset in patterns:
        add('pattern-' + asset['name'], {'pattern': asset['name'], 'pattern_digest': asset['digest']})
    return rows


def fractional_configs(control, factors):
    """Regular half-fraction for 3–5 binary finalists; explicit aliasing, no fit."""
    validate_policy(control)
    if not 3 <= len(factors) <= 5:
        raise ValueError('interaction confirmation requires 3 to 5 binary factors')
    if any(key not in control or key in ('pattern', 'pattern_digest', 'source_version') or
           not isinstance(levels, list) or len(levels) != 2 or levels[0] == levels[1]
           for key, levels in factors.items()):
        raise ValueError('two distinct scalar levels per supported policy factor required')
    keys = sorted(factors)
    rows = [{'label': 'control', 'policy': copy.deepcopy(control)}]
    seen = {policy_digest(control)}
    for row in range(1 << (len(keys)-1)):
        bits = [(row >> bit) & 1 for bit in range(len(keys)-1)]
        bits.append(sum(bits) % 2)
        policy = dict(control, **{key: factors[key][bit] for key, bit in zip(keys, bits)})
        validate_policy(policy)
        key = policy_digest(policy)
        if key not in seen:
            rows.append({'label': 'half-fraction-' + ''.join(map(str, bits)), 'policy': policy})
            seen.add(key)
    return {'configs': rows, 'design': {'factors': factors,
        'generator': 'last sorted factor bit = XOR of preceding factor bits',
        'resolution': len(keys), 'limitation': 'Aliased effects require follow-up; not a full factorial or final policy selection.'}}


def plan(cohort, split, bundle, configs, *, stage='factor', partition='discovery',
         deep_capture=False, round_id='live-round1', max_jobs=256, campaign_bytes=32*GiB, disk_reserve_bytes=8*GiB,
         additional_reserved_bytes=0, free_bytes=None):
    """Reuse every selected original tuple for every policy, never repair draws."""
    if stage not in ('factor', 'interaction', 'confirmation') or partition not in ('discovery', 'holdout'):
        raise ValueError('invalid stage or partition')
    if not isinstance(round_id, str) or not round_id or len(round_id) > 128:
        raise ValueError('explicit bounded live round identity required')
    if type(deep_capture) is not bool or deep_capture and stage != 'confirmation':
        raise ValueError('deep capture is an explicit confirmation-stage option')
    if deep_capture and bundle['capabilities'].get('resource_claims_deep_version') != 1:
        raise ValueError('bundle does not advertise deep capture')
    if partition == 'holdout' and stage != 'confirmation':
        raise ValueError('holdout only permitted in explicit confirmation stage')
    if digest({k: v for k, v in split.items() if k != 'identity'}) != split.get('identity'):
        raise ValueError('frozen split digest mismatch')
    if cohort.get('design', {}).get('split_identity') != split['identity']:
        raise ValueError('cohort does not attest frozen split')
    if not configs or len(configs) > 128:
        raise ValueError('provide 1 to 128 explicit canonical policy configs')
    caps = bundle['capabilities']
    if caps.get('material_destinations_version') != 1 or caps.get('resource_claims_probe_version') != 1:
        raise ValueError('bundle needs destination metadata and active probe capabilities')
    identities = set()
    for row in configs:
        validate_policy(row['policy'], caps)
        key = policy_digest(row['policy'])
        if key in identities or not isinstance(row.get('label'), str) or not row['label']:
            raise ValueError('unique policies and nonempty labels required')
        identities.add(key)
    needs_movement = any(row.get('policy', {}).get('fallback') != 'wait' for row in configs)
    if needs_movement and caps.get('resource_claims_movement_audit_version') != 1:
        raise ValueError('fallback comparisons require all-transport movement audit version 1')
    for value in (max_jobs, campaign_bytes, disk_reserve_bytes):
        if type(value) is not int or value <= 0:
            raise ValueError('positive explicit job/storage bounds required')
    if type(additional_reserved_bytes) is not int or additional_reserved_bytes < 0:
        raise ValueError('invalid additional storage reservation')
    originals = []
    seen = set()
    for original in cohort['jobs']:
        game_id = original['labels']['initial_state_identity']
        if game_id in seen or game_id not in split['games']:
            raise ValueError('cohort must contain unique frozen game identities')
        seen.add(game_id)
        if original['labels']['discovery_split'] != split['games'][game_id]:
            raise ValueError('cohort split label mismatch')
        if split['games'][game_id] == partition:
            originals.append(original)
    if seen != set(split['games']):
        raise ValueError('cohort must retain complete frozen split inventory')
    if not originals or len(originals)*len(configs) > max_jobs:
        raise ValueError('paired stage exceeds job budget; explicitly select a smaller config batch')
    jobs = []
    deep_splits = {}
    if deep_capture:
        for row in configs:
            key = policy_digest(row['policy'])
            deep_cohort = digest(['claims-live-deep-v1', split['cohort_identity'], split['identity'], bundle['id'], key, stage, round_id])
            capture_split = {'version': 1, 'source_identity': bundle['source_revision'],
                'cohort_identity': deep_cohort, 'policy_digest': key,
                'games': copy.deepcopy(split['games']),
                'strata': copy.deepcopy(split.get('strata', {})),
                'original_split_identity': split['identity'], 'round_id': round_id}
            capture_split['identity'] = digest(capture_split)
            deep_splits[key] = capture_split
    for original in originals:
        if original['type'] != 'game' or original['inputs'] or original.get('depends_on'):
            raise ValueError('expected frozen new-game inline tuples without external dependencies')
        for row in configs:
            config = copy.deepcopy(original['config'])
            probe = config.pop('resource_claims_shadow', config.pop('resource_claims_probe', None))
            for key in ('resource_claims_active', 'resource_claims_policy', 'resource_claims_deep_context'):
                config.pop(key, None)
            if not isinstance(probe, dict):
                raise ValueError('frozen cohort must include bounded probe settings')
            config.update(material_destinations=True, resource_claims_probe=probe,
                          resource_claims_policy=copy.deepcopy(row['policy']))
            key = policy_digest(row['policy'])
            labels = dict(original['labels'], claim_mode='experimental-live', claims_enforced=True,
                          variant=key, policy_digest=key, policy_label=row['label'],
                          live_stage=stage, live_round=round_id, source_revision=bundle['source_revision'],
                          paired_original_job=original['id'])
            outputs = copy.deepcopy(original['outputs'])
            # Probability-only logs omit the exact delivery/labour counters.
            # Preserve typed gameplay records for the frozen paired endpoint.
            outputs['log_mode']='gameplay'
            if 'team-timeline' not in outputs.setdefault('telemetry',[]):
                outputs['telemetry'].append('team-timeline')
            labels['require_gameplay_telemetry']=True
            if needs_movement:
                labels['require_movement_audit']=True
            outputs['required'] = [name for name in outputs.get('required', []) if not name.startswith('resource-claims-deep/')]
            if 'resource-claims-shadow.jsonl.gz' not in outputs['required']:
                outputs['required'].append('resource-claims-shadow.jsonl.gz')
            if deep_capture:
                capture_split = deep_splits[key]
                capture_game = labels['initial_state_identity']
                config['resource_claims_deep_context'] = {
                    'source_revision': bundle['source_revision'], 'bundle_id': bundle['id'],
                    'cohort_id': capture_split['cohort_identity'], 'game_id': capture_game}
                labels['deep_game_identity'] = capture_game
                labels['deep_split_identity'] = capture_split['identity']
                outputs['required'].append('resource-claims-deep/manifest.json')
            limits = copy.deepcopy(original['limits'])
            if any(type(limits.get(k)) is not int or limits[k] <= 0 for k in ('artifact_bytes', 'log_bytes')):
                raise ValueError('each attempt needs explicit artifact and log caps')
            limits['log_bytes']=max(limits['log_bytes'],128*1024**2)
            jobs.append(job('game', bundle['id'], seeds=copy.deepcopy(original['seeds']),
                            config=config, outputs=outputs, limits=limits, labels=labels))
    attempt_bytes = sum(j['limits']['artifact_bytes']+j['limits']['log_bytes'] for j in jobs)
    # Reserve an equally-sized transfer/spool duplicate; no assumption of compression savings.
    reserved = 2*attempt_bytes + additional_reserved_bytes
    if reserved > campaign_bytes:
        raise ValueError('worst-case attempts plus spool and additional reservations exceed campaign budget')
    if free_bytes is not None and free_bytes-reserved < disk_reserve_bytes:
        raise ValueError('planned allocation would violate free-space reserve')
    identity = digest([cohort['id'], split['identity'], bundle['id'], stage, partition, round_id, deep_capture, configs])
    result = {'schema_version': 1, 'id': f'claims-live-{stage}-{bundle["source_revision"][:10]}-{identity[:12]}',
        'kind': 'ai_comparison', 'jobs': jobs,
        'settings': {'artifact_budget_bytes': campaign_bytes, 'disk_reserve_bytes': disk_reserve_bytes,
                     'process_attempts': 1, 'infrastructure_attempts': 1},
        'labels': {'source_cohort': cohort['id'], 'stage': stage, 'partition': partition},
        'design': {'split_identity': split['identity'], 'cohort_identity': split['cohort_identity'],
            'source_revision': bundle['source_revision'], 'configs': configs, 'max_jobs': max_jobs, 'round_id': round_id,
            'paired_games': len(originals), 'reserved_bytes': reserved,
            'additional_reserved_bytes': additional_reserved_bytes,
            'storage_model': 'one attempt per job plus equally sized spool/transfer copy; retries require a new budget review; explicit other reservations',
            'selection_allowed': False, 'dispatch_authorized': False,
            'paired_inputs': 'Original tuple/seeds/seats/stopping rule and split preserved; no policy-dependent resampling.',
            'deep_capture': deep_capture, 'deep_splits': deep_splits,
            'deep_context_identity': 'Per-policy, source bundle and round, independent of batch membership; original game IDs and split roles preserved.',
            'release_gate': 'Native validation, resource allocation, and explicit stage review before submission.'}}
    validate_experiment(result)
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest='command', required=True)
    factors = sub.add_parser('factors'); factors.add_argument('control'); factors.add_argument('output'); factors.add_argument('--patterns')
    interactions = sub.add_parser('interactions'); interactions.add_argument('control'); interactions.add_argument('factors'); interactions.add_argument('output')
    create = sub.add_parser('plan')
    for name in ('cohort', 'split', 'bundle', 'configs', 'output'): create.add_argument(name)
    create.add_argument('--stage', choices=('factor', 'interaction', 'confirmation'), default='factor')
    create.add_argument('--partition', choices=('discovery', 'holdout'), default='discovery')
    create.add_argument('--round-id', default='live-round1')
    create.add_argument('--deep-capture', action='store_true')
    create.add_argument('--max-jobs', type=int, default=256)
    create.add_argument('--campaign-bytes', type=int, default=32*GiB)
    create.add_argument('--disk-reserve-bytes', type=int, default=8*GiB)
    create.add_argument('--additional-reserved-bytes', type=int, required=True)
    args = parser.parse_args()
    read = lambda path: json.loads(Path(path).read_text())
    if args.command == 'factors':
        result = {'configs': factor_configs(read(args.control), read(args.patterns) if args.patterns else ())}
    elif args.command == 'interactions':
        result = fractional_configs(read(args.control), read(args.factors))
    else:
        result = plan(read(args.cohort), read(args.split), read(args.bundle), read(args.configs)['configs'],
            stage=args.stage, partition=args.partition, deep_capture=args.deep_capture, round_id=args.round_id, max_jobs=args.max_jobs,
            campaign_bytes=args.campaign_bytes, disk_reserve_bytes=args.disk_reserve_bytes,
            additional_reserved_bytes=args.additional_reserved_bytes,
            free_bytes=shutil.disk_usage(Path(args.output).resolve().parent).free)
    with Path(args.output).open('x') as stream:
        json.dump(result, stream, indent=2); stream.write('\n')


if __name__ == '__main__': main()
