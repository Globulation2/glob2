"""Bounded live journey contracts; journey samples are clustered by game."""
import re

EVENTS = ('acquired','released','retargeted','harvest_begin','harvested','harvest_failed',
          'goal_refresh_same','goal_refresh_changed','goal_refresh_failed','fallback_wait','fallback_roam',
          'fallback_release','route_initial','route_refresh','route_repair','route_probe_fallback','pending_path',
          'traffic_wait','geometry_contradiction','reanchor_joined','reanchor_failed','probe_tail_failed','obsolete_probe')
TERMINALS = {'harvested','harvest_failed','retargeted','released','timeout','end_of_segment'}
RELEASE_REASONS = ('harvest','route_failed','unit_state_changed','resource_depleted','access_invalid','pickup_mismatch',
                   'job_ended','medical','unit_removed','serialization','map_invalidation','configuration','other')
SELECTION_FILTERS = ('terrain_prefilter_rejections','refresh_margin_rejections','refresh_margin_kept')
POLICY_EXTENSIONS = ('pending_gradient_steps','traffic_sidesteps','traffic_wait_limit_repairs',
                     'distance_factor_rejections','fallback_gradient_decisions','claim_gradient_signals')
MOVEMENT_KINDS = ('medical','claimed_resource','unclaimed_resource','return_to_job','idle','other')


def natural(value, name, maximum=2**64-1):
    if type(value) is not int or not 0 <= value <= maximum:
        raise ValueError('invalid journey ' + name)
    return value


def validate_movement_audit(version, audit):
    if version is None:
        if audit is not None:
            raise ValueError('unadvertised movement audit')
        return
    if type(version) is not int or version != 1 or not isinstance(audit, dict) or type(audit.get('version')) is not int or audit['version'] != 1:
        raise ValueError('unsupported movement audit')
    if (audit.get('scope') != 'all-transport-committed-grid-moves' or
            not isinstance(audit.get('kind_names'), list) or tuple(audit['kind_names']) != MOVEMENT_KINDS):
        raise ValueError('invalid movement audit scope/kinds')
    start, end = audit.get('coverage_start'), audit.get('coverage_end')
    if start is not None:
        natural(start, 'movement coverage start', 2**32-1)
    if end is not None:
        natural(end, 'movement coverage end', 2**32-1)
    if audit.get('coverage_end_exclusive') is not True:
        raise ValueError('movement coverage requires an exclusive end')
    known = audit.get('coverage_known')
    if type(known) is not bool or known != (start is not None and end is not None and start <= end):
        raise ValueError('invalid movement coverage availability')
    values = []
    for name in ('moves', 'weighted_cost'):
        array = audit.get(name)
        if not isinstance(array, list) or len(array) != len(MOVEMENT_KINDS):
            raise ValueError('invalid movement audit counts')
        values.append([natural(value, 'movement '+name) for value in array])
    moves, costs = values
    if any((move == 0) != (cost == 0) or cost < move for move, cost in zip(moves, costs)):
        raise ValueError('inconsistent committed movement costs')
    if natural(audit.get('total_moves'), 'movement total') != sum(moves) or natural(audit.get('total_weighted_cost'), 'movement total cost') != sum(costs):
        raise ValueError('movement audit totals disagree')
    natural(audit.get('excluded_non_neighbor_moves'), 'movement excluded non-neighbor')
    if audit.get('unclaimed_source_separation_available') is not False:
        raise ValueError('unclaimed natural/supplier separation is unavailable')


class Validator:
    def __init__(self, schema):
        self.movement_version = schema.get('movement_audit_version')
        if self.movement_version is not None and (type(self.movement_version) is not int or self.movement_version != 1):
            raise ValueError('unsupported movement audit version')
        self.enabled = 'live_journey_version' in schema
        if self.enabled and (type(schema['live_journey_version']) is not int or schema['live_journey_version'] != 1 or tuple(schema.get('live_event_names', ())) != EVENTS):
            raise ValueError('unsupported live journey schema')
        self.sample_every=schema.get('sample_every');self.max_samples=schema.get('max_samples')
        if self.enabled:
            for name,value in (('sample_every',self.sample_every),('max_samples',self.max_samples)):
                if value is not None and not natural(value,name):raise ValueError('positive journey schema bound required')
        self.ids = set(); self.censored = 0; self.steps = 0; self.cost = 0
        self.events = [0]*len(EVENTS); self.phases = [0]*16
        # Release reasons and selection filters are additive; None for older traces.
        self.release_reasons = None; self.selection_filters = None; self.policy_extensions = None
        self.journey_release_reasons = {}

    def row(self, row):
        if not self.enabled or type(row.get('schema_version')) is not int or row['schema_version'] != 1:
            raise ValueError('unadvertised or unsupported journey row')
        for name in ('owner','task','acquired_tick','end_tick','origin','source','access','material','steps','live_cost'):
            natural(row.get(name), name, 65535 if name == 'material' else 2**32-1 if name in ('acquired_tick','end_tick','origin','source','access') else 2**64-1)
        if not row['owner'] or not row['task'] or row['end_tick'] < row['acquired_tick']:
            raise ValueError('invalid journey identity/ticks')
        key = row['owner'], row['task']
        if key in self.ids or len(self.ids) >= 512:
            raise ValueError('duplicate or excessive journey rows')
        if row.get('terminal') not in TERMINALS:
            raise ValueError('unknown journey terminal')
        if row['terminal']=='timeout' and row['end_tick']-row['acquired_tick']<65536:
            raise ValueError('journey timeout precedes configured horizon')
        if (row['steps'] == 0) != (row['live_cost'] == 0):
            raise ValueError('inconsistent journey committed steps/cost')
        digest = row.get('policy_digest')
        if not isinstance(digest, str) or digest and not re.fullmatch('[0-9a-f]{64}', digest):
            raise ValueError('invalid journey policy digest')
        events = row.get('events')
        if not isinstance(events, list) or len(events) != len(EVENTS):
            raise ValueError('invalid journey event counts')
        for value in events: natural(value, 'event count')
        if events[0] != 1:
            raise ValueError('journey must have one acquisition')
        status = row.get('baseline_status')
        if status not in ('reached','unreachable','limit','not_ready'):
            raise ValueError('unknown journey baseline status')
        for name in ('baseline_cost','baseline_expansions','baseline_elapsed_ns'):
            unavailable = (status != 'reached' if name == 'baseline_cost' else status == 'not_ready')
            if unavailable:
                if row.get(name) is not None: raise ValueError('unavailable journey baseline must be null')
            else: natural(row.get(name), name)
        if 'release_reason' in row:
            reason = row['release_reason']
            if reason is not None and (reason not in RELEASE_REASONS or row['terminal'] != 'released'):
                raise ValueError('invalid journey release reason')
            if reason is not None:
                self.journey_release_reasons[reason] = self.journey_release_reasons.get(reason, 0) + 1
        self.ids.add(key);self.steps += row['steps'];self.cost += row['live_cost']
        self.censored += row['terminal'] in ('timeout','end_of_segment')
        self.events = [a+b for a,b in zip(self.events, events)]
        self.phases[min(row['acquired_tick']//4096,15)] += 1

    def finish(self, summary):
        validate_movement_audit(self.movement_version, summary.get('movement_audit'))
        live = summary.get('live')
        if not self.enabled:
            if live is not None: raise ValueError('unadvertised journey summary')
            return
        if not isinstance(live, dict) or type(live.get('version')) is not int or live['version'] != 1:
            raise ValueError('missing journey summary')
        for key in ('journey_eligible','journey_sampled','journey_admitted','journey_completed','excluded_capacity',
                    'excluded_limit','actual_moves','actual_weighted_cost','untracked_claim_moves','sample_every',
                    'max_active','max_admissions','timeout_ticks'):
            natural(live.get(key), key)
        if live['timeout_ticks']!=65536 or self.sample_every is not None and live['sample_every']!=self.sample_every:
            raise ValueError('journey horizon/sampling differs from schema contract')
        if self.max_samples is not None and live['max_admissions']!=min(512,self.max_samples):
            raise ValueError('journey cap differs from advertised sample limit')
        if 'scope' in live and live['scope']!='held-material-claim-pursuit-only':
            raise ValueError('unsupported live movement scope')
        if 'unclaimed_movement_available' in live and live['unclaimed_movement_available'] is not False:
            raise ValueError('unclaimed movement is outside journey version1 scope')
        if not 1 <= live['max_active'] <= 8 or not 1 <= live['max_admissions'] <= 512 or not live['sample_every']:
            raise ValueError('invalid journey admission bounds')
        if not (len(self.ids) == live['journey_completed'] == live['journey_admitted'] <= live['max_admissions']):
            raise ValueError('incomplete journey trace')
        if not live['journey_admitted'] <= live['journey_sampled'] <= live['journey_eligible']:
            raise ValueError('inconsistent journey sampling counts')
        excluded = live['excluded_capacity']+live['excluded_limit']
        if live['journey_sampled'] != live['journey_admitted']+excluded:
            raise ValueError('inconsistent journey admission exclusions')
        if self.steps+live['untracked_claim_moves'] != live['actual_moves'] or self.cost > live['actual_weighted_cost']:
            raise ValueError('journey movement counters disagree')
        counts = live.get('event_counts')
        if not isinstance(counts,list) or len(counts) != len(EVENTS):raise ValueError('invalid live event counters')
        for total, observed in zip(counts,self.events):
            if natural(total,'event count') < observed:raise ValueError('journey events exceed game totals')
        for key, expected in (('sampling_censored',bool(excluded)),('admission_uncensored',not excluded)):
            if key in live and (type(live[key]) is not bool or live[key] != expected):raise ValueError('inconsistent journey censor flag')
        if 'censored_journey_outcomes' in live and live['censored_journey_outcomes'] != self.censored:
            raise ValueError('inconsistent journey partial outcomes')
        if 'release_reason_counts' in live:
            reasons = live['release_reason_counts']
            if (type(live.get('release_reason_version')) is not int or live['release_reason_version'] != 1
                    or live.get('release_reason_names') != list(RELEASE_REASONS)
                    or not isinstance(reasons, list) or len(reasons) != len(RELEASE_REASONS)):
                raise ValueError('invalid release reason counters')
            if sum(natural(x, 'release reason count') for x in reasons) != counts[EVENTS.index('released')]:
                raise ValueError('release reasons disagree with released events')
            self.release_reasons = dict(zip(RELEASE_REASONS, reasons))
        if 'selection_filter_counts' in live:
            filters = live['selection_filter_counts']
            if not isinstance(filters, dict) or set(filters) != set(SELECTION_FILTERS):
                raise ValueError('invalid selection filter counters')
            self.selection_filters = {key: natural(filters[key], key) for key in SELECTION_FILTERS}
        if 'policy_extension_counts' in live or 'policy_extension_version' in live:
            extensions = live.get('policy_extension_counts')
            if (type(live.get('policy_extension_version')) is not int or live['policy_extension_version'] != 1
                    or not isinstance(extensions, dict) or set(extensions) != set(POLICY_EXTENSIONS)):
                raise ValueError('invalid policy extension counters')
            self.policy_extensions = {key: natural(extensions[key], key) for key in POLICY_EXTENSIONS}
        if 'route_status_counts' in live:
            statuses=live['route_status_counts']
            if live.get('route_status_names') != ['found','unreachable','cost_limit','expansion_limit'] or not isinstance(statuses,list) or len(statuses)!=4:
                raise ValueError('invalid route status counters')
            if sum(natural(x,'route status count') for x in statuses)!=natural(live.get('route_completed'),'route completed'):
                raise ValueError('route status total mismatch')
        for key in ('route_canceled','route_expansions','route_worker_elapsed_ns','route_unobserved_work',
                    'route_restored_outputs','baseline_unpublished_checks','baseline_unpublished_at_finish',
                    'baseline_blocking_finish_joins','baseline_late_publications','baseline_max_publish_delay_ticks'):
            if key in live:natural(live[key],key)
        fields = ('eligible_by_phase','sampled_by_phase','admitted_by_phase','excluded_by_phase')
        if any(key in live for key in fields):
            if live.get('phase_bin_ticks',4096)!=4096 or live.get('phase_last_bin_starts',61440)!=61440:
                raise ValueError('unsupported journey phase boundaries')
            for key,total in zip(fields,(live['journey_eligible'],live['journey_sampled'],live['journey_admitted'],excluded)):
                values=live.get(key)
                if not isinstance(values,list) or len(values)!=16:raise ValueError('invalid journey phase bins')
                if sum(natural(x,'phase count') for x in values)!=total:raise ValueError('journey phase total mismatch')
            if live['admitted_by_phase'] != self.phases:raise ValueError('journey phase rows mismatch')
            for eligible,sampled,admitted,omitted in zip(*(live[key] for key in fields)):
                if not admitted<=sampled<=eligible or sampled!=admitted+omitted:raise ValueError('inconsistent journey phase coverage')


def attest_digests(digests, resolved):
    if not digests:return
    mode = resolved.get('active_claim_policy')
    if mode == 'experimental':
        from .claim_policy import attested_policy_digest
        expected = attested_policy_digest(resolved)
    elif mode == 'prototype-stationary-v1':expected = ''
    else:raise ValueError('journeys require an attested active policy')
    if set(digests) != {expected}:raise ValueError('journey policy digest differs from engine result')
