"""Shared valid version-2 resource-claim probe trace for tournament reader tests."""
import copy
import io
import json

from tools.tournaments.claim_journeys import (CONTENTION_COUNTS, EVENTS, MOVEMENT_KINDS, POLICY_EXTENSIONS,
                                              RELEASE_REASONS, SELECTION_FILTERS)
from tools.tournaments.claim_outcomes import COUNTERS
from tools.tournaments.claim_policy import policy_digest

POLICY = {'source_version': 1, 'pattern': 'spiral:k1', 'pattern_digest': 'a'*64, 'selection': 'first_available',
          'fallback': 'wait', 'fallback_after_decisions': 16, 'destination_refresh_j': 0, 'route_refresh_j': 0,
          'route_target': 'claimed_access', 'pending_step': 'wait', 'maximum_expansions': 4096, 'use_primary_gradient': False}


def movement_audit(moves=(0, 2, 0, 0, 0, 0), costs=(0, 20, 0, 0, 0, 0)):
    return {'version': 1, 'scope': 'all-transport-committed-grid-moves', 'kind_names': list(MOVEMENT_KINDS),
            'coverage_start': 5, 'coverage_end': 10, 'coverage_end_exclusive': True, 'coverage_known': True,
            'moves': list(moves), 'weighted_cost': list(costs), 'total_moves': sum(moves), 'total_weighted_cost': sum(costs),
            'excluded_non_neighbor_moves': 0, 'unclaimed_source_separation_available': False}


def outcome_footer(counts=None):
    table = [[[0]*len(COUNTERS) for _ in range(2)] for _ in range(12)]
    for (material, phase), values in (counts or {}).items():
        for key, value in values.items():
            table[material][phase][COUNTERS.index(key)] = value
    return {'version': 2, 'phase_names': ['movement', 'pickup'], 'counter_names': list(COUNTERS),
            'material_phase_counters': table, 'scope': 'fixture'}


def trace(policy=None, *, journeys=1, movement_only=False):
    """[schema, journey..., summary] for one active (or movement-only) game."""
    policy = policy or POLICY
    schema = {'kind': 'schema', 'version': 2, 'sample_every': 1, 'max_samples': 32, 'max_output_bytes': 1048576,
              'movement_audit_version': 1, 'movement_only': movement_only,
              'live_journey_version': 1, 'live_event_names': list(EVENTS), 'decision_outcome_version': 2}
    events = [0]*len(EVENTS)
    rows = []
    for index in range(journeys):
        journey_events = [0]*len(EVENTS); journey_events[0] = 1
        rows.append({'kind': 'journey', 'schema_version': 1, 'owner': 65536+index, 'task': 1, 'acquired_tick': 5, 'end_tick': 10,
                     'origin': 0, 'source': 8, 'access': 8, 'material': 1, 'terminal': 'end_of_segment', 'steps': 2, 'live_cost': 20,
                     'events': journey_events, 'policy_digest': policy_digest(policy), 'baseline_status': 'reached',
                     'baseline_cost': 10, 'baseline_expansions': 10, 'baseline_elapsed_ns': 200, 'release_reason': None})
        events[0] += 1
    live = {'version': 1, 'journey_eligible': journeys, 'journey_sampled': journeys, 'journey_admitted': journeys,
            'journey_completed': journeys, 'excluded_capacity': 0, 'excluded_limit': 0,
            'actual_moves': 2*journeys, 'actual_weighted_cost': 20*journeys, 'untracked_claim_moves': 0,
            'sample_every': 1, 'max_active': 8, 'max_admissions': 32, 'timeout_ticks': 65536, 'event_counts': events,
            'scope': 'held-material-claim-pursuit-only', 'unclaimed_movement_available': False,
            'sampling_censored': False, 'admission_uncensored': True, 'censored_journey_outcomes': journeys,
            'eligible_by_phase': [journeys]+[0]*15, 'sampled_by_phase': [journeys]+[0]*15,
            'admitted_by_phase': [journeys]+[0]*15, 'excluded_by_phase': [0]*16,
            'release_reason_version': 1, 'release_reason_names': list(RELEASE_REASONS), 'release_reason_counts': [0]*len(RELEASE_REASONS),
            'selection_filter_counts': {key: 0 for key in SELECTION_FILTERS},
            'policy_extension_version': 1, 'policy_extension_counts': {key: 0 for key in POLICY_EXTENSIONS},
            'contention_version': 1, 'contention_counts': {key: 0 for key in CONTENTION_COUNTS}}
    summary = {'kind': 'summary', 'complete': True, 'capture_bytes': 0, 'live': live,
               'decision_outcomes': outcome_footer(), 'movement_audit': movement_audit((0, 2*journeys, 0, 0, 0, 0), (0, 20*journeys, 0, 0, 0, 0))}
    return [schema, *rows, summary]


def job_and_result(policy=None, *, movement_only=False):
    policy = policy or POLICY
    resolved = {'active_claim_policy': 'experimental', 'material_destinations_enabled': True,
                'resource_claims_policy_config': policy, 'resource_claims_policy_digest': policy_digest(policy)}
    result = {'schema_version': 1, 'status': 'completed', 'resolved': resolved,
              'resource_claims_shadow': {'schema_version': 2, 'enforced': True, 'movement_only': movement_only}}
    observer = 'resource_claims_movement_only' if movement_only else 'resource_claims_probe'
    job = {'config': {'resource_claims_policy': copy.deepcopy(policy), 'material_destinations': True,
                      observer: {'sample_every': 1, 'max_samples': 32, 'max_bytes': 1048576}}}
    return job, result


def stream(rows):
    return io.StringIO(''.join(json.dumps(row)+'\n' for row in rows))
