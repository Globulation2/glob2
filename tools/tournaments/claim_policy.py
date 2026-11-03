"""Temporary frozen live claim policy contract; standard-library worker safe."""
import hashlib
import json
import re

INTERVALS = {0, 1, 2, 3, 4, 6, 8, 12, 16, 24, 32, 48, 64, 96, 128}
ENUMS = {
    'selection': {'first_available', 'closest_geometric'},
    'fallback': {'wait', 'roam', 'release_job', 'bounded_wait_release'},
    'route_target': {'claimed_access', 'probe_then_gradient'},
    'pending_step': {'wait', 'direct_heading'},
}
KEYS = set(ENUMS) | {'source_version', 'pattern', 'pattern_digest',
    'fallback_after_decisions', 'destination_refresh_j', 'route_refresh_j',
    'maximum_expansions', 'use_primary_gradient'}
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
    if type(version) is not int or version not in (1, 2):
        raise ValueError('unsupported claim policy source version')
    expected_keys = KEYS | (set(RECOVERY_V1) if version == 2 else set())
    if set(policy) != expected_keys:
        raise ValueError('claim policy requires all canonical configuration fields')
    if version == 2:
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
    if type(policy['use_primary_gradient']) is not bool:
        raise ValueError('claim primary gradient option must be boolean')
    if capabilities is not None:
        supported = capabilities.get('resource_claims_policy_config_version')
        if type(supported) is not int or supported not in (1, 2) or supported < version or 'experimental' not in capabilities.get('resource_claims_active_policies', []):
            raise ValueError('build does not advertise claim policy config version ' + str(version))
        assets = capabilities.get('resource_claims_policy_patterns', [])
        if {'name': policy['pattern'], 'digest': policy['pattern_digest']} not in assets:
            raise ValueError('claim policy asset absent from pinned build catalog')
    return policy


def canonical_policy(policy):
    validate_policy(policy)
    return json.dumps(policy, sort_keys=True, separators=(',', ':'), ensure_ascii=False, allow_nan=False)


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
