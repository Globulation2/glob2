"""Temporary frozen live claim policy contract; standard-library worker safe."""
import hashlib
import json
import re
import struct

INTERVALS = {0, 1, 2, 3, 4, 6, 8, 12, 16, 24, 32, 48, 64, 96, 128}
ENUMS = {
    'selection': {'first_available', 'closest_geometric'},
    'fallback': {'wait', 'roam', 'release_job', 'bounded_wait_release'},
    'route_target': {'claimed_access', 'probe_then_gradient'},
    'pending_step': {'wait', 'direct_heading', 'gradient'},
}
KEYS = set(ENUMS) | {'source_version', 'pattern', 'pattern_digest',
    'fallback_after_decisions', 'destination_refresh_j', 'route_refresh_j',
    'maximum_expansions', 'use_primary_gradient'}
# Optional experiments accepted by every source version. Like the engine, the
# canonical form omits a default value, so existing policies keep their digest.
OPTIONS = {
    'refresh_switch_margin_permille': 0,
    'terrain_component_prefilter': False,
    'claim_oversubscription_permille': 1000,
    'max_claim_distance_factor_permille': 0,
}
# Enum values added after their key; a build must advertise them by name.
ENUM_OPTIONS = {
    ('pending_step', 'gradient'): 'pending_step_gradient',
}
MAXIMUM_REFRESH_SWITCH_MARGIN_PERMILLE = 900
# Claimants per source stay below spendable*permille/1000 (integer, rounded
# down); 1000 is exact capacity and 'unlimited' removes the count limit.
MINIMUM_CLAIM_OVERSUBSCRIPTION_PERMILLE = 1000
MAXIMUM_CLAIM_OVERSUBSCRIPTION_PERMILLE = 100000
# Candidates beyond permille/1000 of the nearest destination distance plus
# ClaimDistanceSlackTiles (4) are rejected; 0 disables the cap.
MINIMUM_CLAIM_DISTANCE_FACTOR_PERMILLE = 1000
MAXIMUM_CLAIM_DISTANCE_FACTOR_PERMILLE = 100000
RECOVERY_V1 = {
    'recovery_source_version': 1,
    'recovery_cache_entries': 4,
    'recovery_ttl_ticks': 128,
    'recovery_attempt_window_ticks': 64,
    'recovery_max_attempts': 4,
    'recovery_backoff_ticks': 16,
    'recovery_selection': 'first_available_cursor',
}


def validate_policy(policy, capabilities=None):
    # Require the complete canonical config. An omitted default must not silently
    # change meaning when a future executable changes its experimental defaults.
    if not isinstance(policy, dict):
        raise ValueError('claim policy requires all canonical configuration fields')
    version = policy.get('source_version')
    if type(version) is not int or version not in (1, 2, 3, 4, 5):
        raise ValueError('unsupported claim policy source version')
    expected_keys = KEYS | (set(RECOVERY_V1) if version >= 2 else set()) | ({'opportunistic_adjacent_pickup'} if version >= 3 else set()) | ({'direct_resource_probes'} if version == 4 else set()) | ({'pattern_format', 'pattern_frame'} if version == 5 else set())
    if set(policy) - set(OPTIONS) != expected_keys:
        raise ValueError('claim policy requires all canonical configuration fields')
    margin = policy.get('refresh_switch_margin_permille', 0)
    if type(margin) is not int or not 0 <= margin <= MAXIMUM_REFRESH_SWITCH_MARGIN_PERMILLE:
        raise ValueError('invalid claim refresh switch margin')
    if type(policy.get('terrain_component_prefilter', False)) is not bool:
        raise ValueError('claim terrain component prefilter option must be boolean')
    oversubscription = policy.get('claim_oversubscription_permille', 1000)
    if oversubscription != 'unlimited' and (type(oversubscription) is not int or not
            MINIMUM_CLAIM_OVERSUBSCRIPTION_PERMILLE <= oversubscription <= MAXIMUM_CLAIM_OVERSUBSCRIPTION_PERMILLE):
        raise ValueError('invalid claim oversubscription permille')
    distance = policy.get('max_claim_distance_factor_permille', 0)
    if type(distance) is not int or not (distance == 0 or
            MINIMUM_CLAIM_DISTANCE_FACTOR_PERMILLE <= distance <= MAXIMUM_CLAIM_DISTANCE_FACTOR_PERMILLE):
        raise ValueError('invalid claim distance factor permille')
    if version >= 2:
        for key, value in RECOVERY_V1.items():
            if type(policy[key]) is not type(value) or policy[key] != value:
                raise ValueError('unsupported claim recovery parameter: ' + key)
    if not isinstance(policy['pattern'], str) or not re.fullmatch(r'[a-z0-9:_-]{1,256}', policy['pattern']):
        raise ValueError('invalid claim policy pattern identity')
    if not isinstance(policy['pattern_digest'], str) or not re.fullmatch(r'[0-9a-f]{64}', policy['pattern_digest']):
        raise ValueError('claim policy needs exact pattern asset digest')
    for key, choices in ENUMS.items():
        if not isinstance(policy[key], str) or policy[key] not in choices:
            raise ValueError('unknown claim policy ' + key)
    for key in ('destination_refresh_j', 'route_refresh_j'):
        if type(policy[key]) is not int or policy[key] not in INTERVALS:
            raise ValueError('invalid claim policy refresh interval')
    for key, maximum in (('fallback_after_decisions', 65535), ('maximum_expansions', 4096)):
        if type(policy[key]) is not int or not 1 <= policy[key] <= maximum:
            raise ValueError('invalid claim policy bound: ' + key)
    if version >= 3 and type(policy['opportunistic_adjacent_pickup']) is not bool:
        raise ValueError('claim adjacent pickup option must be boolean')
    if version == 4 and type(policy['direct_resource_probes']) is not bool:
        raise ValueError('claim direct resource probes option must be boolean')
    if version == 5:
        if policy['pattern_format'] != 'mixed-v1' or policy['pattern_frame'] not in ('mapaxes', 'nearest-d4-v1'):
            raise ValueError('unsupported mixed pattern format/frame')
        if policy['selection'] != 'first_available':
            raise ValueError('mixed policy currently requires first_available')
    if type(policy['use_primary_gradient']) is not bool:
        raise ValueError('claim primary gradient option must be boolean')
    if capabilities is not None:
        supported = capabilities.get('resource_claims_policy_config_version')
        if type(supported) is not int or supported not in (1, 2, 3, 4, 5) or supported < version or 'experimental' not in capabilities.get('resource_claims_active_policies', []):
            raise ValueError('build does not advertise claim policy config version ' + str(version))
        options = capabilities.get('resource_claims_policy_options', [])
        for key, default in OPTIONS.items():
            if policy.get(key, default) != default and (not isinstance(options, list) or key not in options):
                raise ValueError('build does not advertise claim policy option ' + key)
        for (key, value), name in ENUM_OPTIONS.items():
            if policy[key] == value and (not isinstance(options, list) or name not in options):
                raise ValueError('build does not advertise claim policy option ' + name)
        assets = capabilities.get('resource_claims_policy_mixed_patterns' if version == 5 else 'resource_claims_policy_patterns', [])
        asset = {'name': policy['pattern'], 'digest': policy['pattern_digest']}
        if version == 5:
            asset.update(format=policy['pattern_format'], frame=policy['pattern_frame'])
        if asset not in assets:
            raise ValueError('claim policy asset absent from pinned build catalog')
    return policy


def normalized_policy(policy):
    """The engine's canonical object: optional options at their default are omitted."""
    validate_policy(policy)
    return {key: value for key, value in policy.items() if key not in OPTIONS or value != OPTIONS[key]}


def canonical_policy(policy):
    return json.dumps(normalized_policy(policy), sort_keys=True, separators=(',', ':'), ensure_ascii=False, allow_nan=False)


def policy_digest(policy):
    return hashlib.sha256(canonical_policy(policy).encode()).hexdigest()


def attested_policy_digest(resolved):
    """Validate a new or saved Experimental result without inheriting defaults."""
    if not isinstance(resolved, dict) or resolved.get('active_claim_policy') != 'experimental':
        raise ValueError('engine did not attest experimental claim policy')
    if resolved.get('material_destinations_enabled') is not True:
        raise ValueError('experimental claim policy requires destination metadata')
    actual = resolved.get('resource_claims_policy_config')
    validate_policy(actual)
    identity = policy_digest(actual)
    if resolved.get('resource_claims_policy_digest') != identity:
        raise ValueError('engine experimental claim policy digest does not match config')
    return identity


def mixed_pattern_digest(entries, frame):
    """Exact mixed-v1 asset bytes; never reinterpret legacy 5-byte probes."""
    if frame not in ('mapaxes', 'nearest-d4-v1'):
        raise ValueError('unsupported mixed pattern frame')
    data = bytearray(('mixed-v1:' + frame + ':').encode())
    count = 0
    for x, y, tag in entries:
        if any(type(v) is not int for v in (x, y, tag)) or not (-32768 <= x <= 32767 and -32768 <= y <= 32767 and 0 <= tag <= 3):
            raise ValueError('invalid mixed-v1 entry')
        data.extend(struct.pack('<hhB', x, y, tag)); count += 1
        if count > 65535:
            raise ValueError('mixed asset entry bound')
    if not count:
        raise ValueError('empty mixed asset')
    return hashlib.sha256(data).hexdigest()
