#!/usr/bin/env python3
"""Optional claim policy experiments keep canonical identities and engine parity."""
import copy
import json
from pathlib import Path
import sys
import unittest
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from tools.tournaments.claim_policy import (OPTIONS, RECOVERY_V1, canonical_policy, normalized_policy,
    policy_digest, validate_policy)
from tools.tournaments.claim_journeys import EVENTS, RELEASE_REASONS, Validator


def policy(version=2):
    result = {'source_version': version, 'pattern': 'spiral:k16:unit:s1:every:forward:d0:seed1',
        'pattern_digest': 'a'*64, 'selection': 'first_available', 'fallback': 'wait',
        'fallback_after_decisions': 16, 'destination_refresh_j': 0, 'route_refresh_j': 0,
        'route_target': 'claimed_access', 'pending_step': 'wait', 'maximum_expansions': 4096,
        'use_primary_gradient': False}
    if version >= 2:
        result |= RECOVERY_V1
    if version >= 3:
        result['opportunistic_adjacent_pickup'] = False
    return result


# Canonical JSON and digest the engine (ClaimPolicyConfig::canonicalJson and
# digest(), reported as resolved.resource_claims_policy_config/_digest) produced
# for the source-5 production policy with each option combination. The first
# digest is also the one the engine produced before these options existed.
PARITY = [
    # options off
    (json.loads('{"destination_refresh_j":12,"fallback":"wait","fallback_after_decisions":16,"maximum_expansions":4096,"opportunistic_adjacent_pickup":true,"pattern":"final-r32-exact24-assembled-v1","pattern_digest":"44af8d18d898b137c62432a56ca0229875b810062502584fad2e2f0976623c3c","pattern_format":"mixed-v1","pattern_frame":"nearest-d4-v1","pending_step":"wait","recovery_attempt_window_ticks":64,"recovery_backoff_ticks":16,"recovery_cache_entries":4,"recovery_max_attempts":4,"recovery_selection":"first_available_cursor","recovery_source_version":1,"recovery_ttl_ticks":128,"route_refresh_j":0,"route_target":"claimed_access","selection":"first_available","source_version":5,"use_primary_gradient":false}'),
     '{"destination_refresh_j":12,"fallback":"wait","fallback_after_decisions":16,"maximum_expansions":4096,"opportunistic_adjacent_pickup":true,"pattern":"final-r32-exact24-assembled-v1","pattern_digest":"44af8d18d898b137c62432a56ca0229875b810062502584fad2e2f0976623c3c","pattern_format":"mixed-v1","pattern_frame":"nearest-d4-v1","pending_step":"wait","recovery_attempt_window_ticks":64,"recovery_backoff_ticks":16,"recovery_cache_entries":4,"recovery_max_attempts":4,"recovery_selection":"first_available_cursor","recovery_source_version":1,"recovery_ttl_ticks":128,"route_refresh_j":0,"route_target":"claimed_access","selection":"first_available","source_version":5,"use_primary_gradient":false}',
     '362f54bc04bd81fb1120c727cbb6413348f0dbd1fa63278d61e1728503c3e9f0'),
    # both options
    (json.loads('{"destination_refresh_j":12,"fallback":"wait","fallback_after_decisions":16,"maximum_expansions":4096,"opportunistic_adjacent_pickup":true,"pattern":"final-r32-exact24-assembled-v1","pattern_digest":"44af8d18d898b137c62432a56ca0229875b810062502584fad2e2f0976623c3c","pattern_format":"mixed-v1","pattern_frame":"nearest-d4-v1","pending_step":"wait","recovery_attempt_window_ticks":64,"recovery_backoff_ticks":16,"recovery_cache_entries":4,"recovery_max_attempts":4,"recovery_selection":"first_available_cursor","recovery_source_version":1,"recovery_ttl_ticks":128,"refresh_switch_margin_permille":250,"route_refresh_j":0,"route_target":"claimed_access","selection":"first_available","source_version":5,"terrain_component_prefilter":true,"use_primary_gradient":false}'),
     '{"destination_refresh_j":12,"fallback":"wait","fallback_after_decisions":16,"maximum_expansions":4096,"opportunistic_adjacent_pickup":true,"pattern":"final-r32-exact24-assembled-v1","pattern_digest":"44af8d18d898b137c62432a56ca0229875b810062502584fad2e2f0976623c3c","pattern_format":"mixed-v1","pattern_frame":"nearest-d4-v1","pending_step":"wait","recovery_attempt_window_ticks":64,"recovery_backoff_ticks":16,"recovery_cache_entries":4,"recovery_max_attempts":4,"recovery_selection":"first_available_cursor","recovery_source_version":1,"recovery_ttl_ticks":128,"refresh_switch_margin_permille":250,"route_refresh_j":0,"route_target":"claimed_access","selection":"first_available","source_version":5,"terrain_component_prefilter":true,"use_primary_gradient":false}',
     '8a2649226bc918d95bc8284f4c1d38ff9d770f6efcc2789b62bfaf58aa1bfeab'),
    # margin250
    (json.loads('{"destination_refresh_j":12,"fallback":"wait","fallback_after_decisions":16,"maximum_expansions":4096,"opportunistic_adjacent_pickup":true,"pattern":"final-r32-exact24-assembled-v1","pattern_digest":"44af8d18d898b137c62432a56ca0229875b810062502584fad2e2f0976623c3c","pattern_format":"mixed-v1","pattern_frame":"nearest-d4-v1","pending_step":"wait","recovery_attempt_window_ticks":64,"recovery_backoff_ticks":16,"recovery_cache_entries":4,"recovery_max_attempts":4,"recovery_selection":"first_available_cursor","recovery_source_version":1,"recovery_ttl_ticks":128,"refresh_switch_margin_permille":250,"route_refresh_j":0,"route_target":"claimed_access","selection":"first_available","source_version":5,"use_primary_gradient":false}'),
     '{"destination_refresh_j":12,"fallback":"wait","fallback_after_decisions":16,"maximum_expansions":4096,"opportunistic_adjacent_pickup":true,"pattern":"final-r32-exact24-assembled-v1","pattern_digest":"44af8d18d898b137c62432a56ca0229875b810062502584fad2e2f0976623c3c","pattern_format":"mixed-v1","pattern_frame":"nearest-d4-v1","pending_step":"wait","recovery_attempt_window_ticks":64,"recovery_backoff_ticks":16,"recovery_cache_entries":4,"recovery_max_attempts":4,"recovery_selection":"first_available_cursor","recovery_source_version":1,"recovery_ttl_ticks":128,"refresh_switch_margin_permille":250,"route_refresh_j":0,"route_target":"claimed_access","selection":"first_available","source_version":5,"use_primary_gradient":false}',
     '38d3a0042f10f34eada69aa62d3e1180c236afed40e09361254f845dff6c8025'),
    # prefilter
    (json.loads('{"destination_refresh_j":12,"fallback":"wait","fallback_after_decisions":16,"maximum_expansions":4096,"opportunistic_adjacent_pickup":true,"pattern":"final-r32-exact24-assembled-v1","pattern_digest":"44af8d18d898b137c62432a56ca0229875b810062502584fad2e2f0976623c3c","pattern_format":"mixed-v1","pattern_frame":"nearest-d4-v1","pending_step":"wait","recovery_attempt_window_ticks":64,"recovery_backoff_ticks":16,"recovery_cache_entries":4,"recovery_max_attempts":4,"recovery_selection":"first_available_cursor","recovery_source_version":1,"recovery_ttl_ticks":128,"route_refresh_j":0,"route_target":"claimed_access","selection":"first_available","source_version":5,"terrain_component_prefilter":true,"use_primary_gradient":false}'),
     '{"destination_refresh_j":12,"fallback":"wait","fallback_after_decisions":16,"maximum_expansions":4096,"opportunistic_adjacent_pickup":true,"pattern":"final-r32-exact24-assembled-v1","pattern_digest":"44af8d18d898b137c62432a56ca0229875b810062502584fad2e2f0976623c3c","pattern_format":"mixed-v1","pattern_frame":"nearest-d4-v1","pending_step":"wait","recovery_attempt_window_ticks":64,"recovery_backoff_ticks":16,"recovery_cache_entries":4,"recovery_max_attempts":4,"recovery_selection":"first_available_cursor","recovery_source_version":1,"recovery_ttl_ticks":128,"route_refresh_j":0,"route_target":"claimed_access","selection":"first_available","source_version":5,"terrain_component_prefilter":true,"use_primary_gradient":false}',
     '1f553328cad74f7dc147fd5e60db23269d0bbc4db2e439048b9a8eb4979223d5'),
]

# Engine-reported (game run resolved config/digest) for the production policy
# with claim_oversubscription_permille 2000 and "unlimited".
OVERSUBSCRIPTION_PARITY = [
    # 2000
    (json.loads('{"claim_oversubscription_permille":2000,"destination_refresh_j":12,"fallback":"wait","fallback_after_decisions":16,"maximum_expansions":4096,"opportunistic_adjacent_pickup":true,"pattern":"final-r32-exact24-assembled-v1","pattern_digest":"44af8d18d898b137c62432a56ca0229875b810062502584fad2e2f0976623c3c","pattern_format":"mixed-v1","pattern_frame":"nearest-d4-v1","pending_step":"wait","recovery_attempt_window_ticks":64,"recovery_backoff_ticks":16,"recovery_cache_entries":4,"recovery_max_attempts":4,"recovery_selection":"first_available_cursor","recovery_source_version":1,"recovery_ttl_ticks":128,"route_refresh_j":0,"route_target":"claimed_access","selection":"first_available","source_version":5,"use_primary_gradient":false}'),
     '{"claim_oversubscription_permille":2000,"destination_refresh_j":12,"fallback":"wait","fallback_after_decisions":16,"maximum_expansions":4096,"opportunistic_adjacent_pickup":true,"pattern":"final-r32-exact24-assembled-v1","pattern_digest":"44af8d18d898b137c62432a56ca0229875b810062502584fad2e2f0976623c3c","pattern_format":"mixed-v1","pattern_frame":"nearest-d4-v1","pending_step":"wait","recovery_attempt_window_ticks":64,"recovery_backoff_ticks":16,"recovery_cache_entries":4,"recovery_max_attempts":4,"recovery_selection":"first_available_cursor","recovery_source_version":1,"recovery_ttl_ticks":128,"route_refresh_j":0,"route_target":"claimed_access","selection":"first_available","source_version":5,"use_primary_gradient":false}',
     'ffc2148b7c9b99a6ed772697c9802f453f494f3ca47a41e6940d8457c91e02ea'),
    # unlimited
    (json.loads('{"claim_oversubscription_permille":"unlimited","destination_refresh_j":12,"fallback":"wait","fallback_after_decisions":16,"maximum_expansions":4096,"opportunistic_adjacent_pickup":true,"pattern":"final-r32-exact24-assembled-v1","pattern_digest":"44af8d18d898b137c62432a56ca0229875b810062502584fad2e2f0976623c3c","pattern_format":"mixed-v1","pattern_frame":"nearest-d4-v1","pending_step":"wait","recovery_attempt_window_ticks":64,"recovery_backoff_ticks":16,"recovery_cache_entries":4,"recovery_max_attempts":4,"recovery_selection":"first_available_cursor","recovery_source_version":1,"recovery_ttl_ticks":128,"route_refresh_j":0,"route_target":"claimed_access","selection":"first_available","source_version":5,"use_primary_gradient":false}'),
     '{"claim_oversubscription_permille":"unlimited","destination_refresh_j":12,"fallback":"wait","fallback_after_decisions":16,"maximum_expansions":4096,"opportunistic_adjacent_pickup":true,"pattern":"final-r32-exact24-assembled-v1","pattern_digest":"44af8d18d898b137c62432a56ca0229875b810062502584fad2e2f0976623c3c","pattern_format":"mixed-v1","pattern_frame":"nearest-d4-v1","pending_step":"wait","recovery_attempt_window_ticks":64,"recovery_backoff_ticks":16,"recovery_cache_entries":4,"recovery_max_attempts":4,"recovery_selection":"first_available_cursor","recovery_source_version":1,"recovery_ttl_ticks":128,"route_refresh_j":0,"route_target":"claimed_access","selection":"first_available","source_version":5,"use_primary_gradient":false}',
     '27ebf691125c94cc2a0b61ca15d2144f8d3f1d7d1d2f6a6fecec1c17d1500263'),
]


class PolicyOptions(unittest.TestCase):
    def test_defaults_are_omitted_and_existing_digests_unchanged(self):
        for version in (1, 2, 3):
            base = policy(version)
            explicit = base | {'refresh_switch_margin_permille': 0, 'terrain_component_prefilter': False}
            self.assertEqual(validate_policy(explicit), explicit)
            self.assertEqual(canonical_policy(explicit), canonical_policy(base))
            self.assertEqual(policy_digest(explicit), policy_digest(base))
            self.assertNotIn('refresh_switch_margin_permille', canonical_policy(explicit))
            self.assertEqual(normalized_policy(explicit), base)
        # Digest frozen before the options existed (test_resource_claim_policy).
        self.assertEqual(policy_digest(policy(1)), 'f5b1257cb0263acb94c41fb96def0016065fc49f5db3c257edd9d4fc368351f4')

    def test_non_default_options_are_canonical_and_change_identity(self):
        base = policy()
        for margin in (1, 250, 900):
            value = base | {'refresh_switch_margin_permille': margin}
            self.assertIn('"refresh_switch_margin_permille":%d' % margin, canonical_policy(value))
            self.assertNotEqual(policy_digest(value), policy_digest(base))
        value = base | {'terrain_component_prefilter': True}
        self.assertIn('"terrain_component_prefilter":true', canonical_policy(value))
        self.assertNotEqual(policy_digest(value), policy_digest(base))

    def test_ranges_and_types_are_strict(self):
        for margin in (-1, 901, 1.0, True, '10', None):
            with self.assertRaises(ValueError):
                validate_policy(policy() | {'refresh_switch_margin_permille': margin})
        for flag in (0, 1, 'true', None):
            with self.assertRaises(ValueError):
                validate_policy(policy() | {'terrain_component_prefilter': flag})
        with self.assertRaises(ValueError):
            validate_policy(policy() | {'refresh_switch_margin': 10})

    def test_capabilities_must_advertise_non_default_options(self):
        caps = {'resource_claims_policy_config_version': 2, 'resource_claims_active_policies': ['experimental'],
            'resource_claims_policy_patterns': [{'name': policy()['pattern'], 'digest': 'a'*64}]}
        validate_policy(policy() | {'refresh_switch_margin_permille': 0}, caps)
        for option in ({'refresh_switch_margin_permille': 100}, {'terrain_component_prefilter': True}):
            with self.assertRaises(ValueError):
                validate_policy(policy() | option, caps)
            validate_policy(policy() | option, caps | {'resource_claims_policy_options': sorted(OPTIONS)})

    def test_engine_canonical_json_and_digest_match(self):
        self.assertTrue(PARITY)
        for value, engine_json, engine_digest in PARITY:
            self.assertEqual(canonical_policy(value), engine_json)
            self.assertEqual(policy_digest(value), engine_digest)
        off = PARITY[0][0] | {'refresh_switch_margin_permille': 0, 'terrain_component_prefilter': False}
        self.assertEqual(policy_digest(off), '362f54bc04bd81fb1120c727cbb6413348f0dbd1fa63278d61e1728503c3e9f0')


class ClaimOversubscription(unittest.TestCase):
    def test_default_is_omitted_and_existing_digests_unchanged(self):
        for version in (1, 2, 3):
            base = policy(version)
            explicit = base | {'claim_oversubscription_permille': 1000}
            self.assertEqual(validate_policy(explicit), explicit)
            self.assertEqual(canonical_policy(explicit), canonical_policy(base))
            self.assertEqual(policy_digest(explicit), policy_digest(base))
            self.assertEqual(normalized_policy(explicit), base)
        production = PARITY[0][0] | {'claim_oversubscription_permille': 1000}
        self.assertEqual(canonical_policy(production), PARITY[0][1])
        self.assertEqual(policy_digest(production), '362f54bc04bd81fb1120c727cbb6413348f0dbd1fa63278d61e1728503c3e9f0')

    def test_non_default_values_are_canonical(self):
        base = policy()
        for value, text in ((1001, '1001'), (2000, '2000'), (100000, '100000'), ('unlimited', '"unlimited"')):
            option = base | {'claim_oversubscription_permille': value}
            self.assertIn('"claim_oversubscription_permille":' + text, canonical_policy(option))
            self.assertNotEqual(policy_digest(option), policy_digest(base))
            self.assertEqual(normalized_policy(option), option)

    def test_domain_is_strict(self):
        for value in (999, 0, -1, 100001, 4294967295, 1500.0, True, '2000', 'Unlimited', None):
            with self.assertRaises(ValueError):
                validate_policy(policy() | {'claim_oversubscription_permille': value})

    def test_capabilities_must_advertise_non_default_value(self):
        caps = {'resource_claims_policy_config_version': 2, 'resource_claims_active_policies': ['experimental'],
            'resource_claims_policy_patterns': [{'name': policy()['pattern'], 'digest': 'a'*64}],
            'resource_claims_policy_options': ['refresh_switch_margin_permille', 'terrain_component_prefilter']}
        validate_policy(policy() | {'claim_oversubscription_permille': 1000}, caps)
        for value in (2000, 'unlimited'):
            with self.assertRaises(ValueError):
                validate_policy(policy() | {'claim_oversubscription_permille': value}, caps)
            validate_policy(policy() | {'claim_oversubscription_permille': value},
                caps | {'resource_claims_policy_options': sorted(OPTIONS)})

    def test_engine_canonical_json_and_digest_match(self):
        self.assertTrue(OVERSUBSCRIPTION_PARITY)
        for value, engine_json, engine_digest in OVERSUBSCRIPTION_PARITY:
            self.assertEqual(canonical_policy(value), engine_json)
            self.assertEqual(policy_digest(value), engine_digest)


def journey_trace(release_reasons=None, filters=None):
    events = [0]*len(EVENTS); events[0] = 1; events[1] = 2
    schema = {'live_journey_version': 1, 'live_event_names': list(EVENTS), 'sample_every': 1, 'max_samples': 32}
    live = {'version': 1, 'journey_eligible': 0, 'journey_sampled': 0, 'journey_admitted': 0, 'journey_completed': 0,
        'excluded_capacity': 0, 'excluded_limit': 0, 'actual_moves': 0, 'actual_weighted_cost': 0,
        'untracked_claim_moves': 0, 'sample_every': 1, 'max_active': 8, 'max_admissions': 32,
        'timeout_ticks': 65536, 'event_counts': events}
    if release_reasons is not None:
        live |= {'release_reason_version': 1, 'release_reason_names': list(RELEASE_REASONS),
            'release_reason_counts': release_reasons}
    if filters is not None:
        live['selection_filter_counts'] = filters
    return schema, {'kind': 'summary', 'live': live}


class ReleaseReasonReader(unittest.TestCase):
    def test_old_traces_remain_valid_without_reasons(self):
        schema, summary = journey_trace()
        reader = Validator(schema); reader.finish(summary)
        self.assertIsNone(reader.release_reasons); self.assertIsNone(reader.selection_filters)

    def test_reasons_are_exposed_and_must_sum_to_released_events(self):
        counts = [0]*len(RELEASE_REASONS); counts[RELEASE_REASONS.index('harvest')] = 1
        counts[RELEASE_REASONS.index('route_failed')] = 1
        filters = {'terrain_prefilter_rejections': 3, 'refresh_margin_rejections': 2, 'refresh_margin_kept': 1}
        schema, summary = journey_trace(counts, filters)
        reader = Validator(schema); reader.finish(summary)
        self.assertEqual(reader.release_reasons['harvest'], 1); self.assertEqual(reader.release_reasons['route_failed'], 1)
        self.assertEqual(reader.selection_filters, filters)
        for broken in (counts[:-1], [0]*len(RELEASE_REASONS)):
            schema, summary = journey_trace(broken)
            with self.assertRaises(ValueError):
                Validator(schema).finish(summary)
        schema, summary = journey_trace(counts); summary['live']['release_reason_names'] = list(reversed(RELEASE_REASONS))
        with self.assertRaises(ValueError):
            Validator(schema).finish(summary)


if __name__ == '__main__':
    unittest.main()
