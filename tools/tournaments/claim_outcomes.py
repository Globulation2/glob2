"""Decision outcome counters of the claim probe footer; counts stay clustered by game."""
from .claim_journeys import natural

COUNTERS = ('eligible','attempted','acquired','held_same','retarget_access','retarget_donor_same_access',
            'no_candidate','not_attempted','cooldown_not_attempted','missing_field_not_attempted',
            'route_ready','route_pending','route_failed','route_arrived','fallback_wait','fallback_roam','fallback_release','cooldown_wait')
VERSION = 2


class Validator:
    def __init__(self, schema):
        self.version = schema.get('decision_outcome_version')
        if self.version is not None and (type(self.version) is not int or self.version != VERSION):
            raise ValueError('unsupported decision outcome version')

    def finish(self, summary):
        footer = summary.get('decision_outcomes')
        if self.version is None:
            if footer is not None: raise ValueError('unadvertised outcome footer')
            return None
        if (not isinstance(footer, dict) or footer.get('version') != VERSION or tuple(footer.get('counter_names', ())) != COUNTERS
                or footer.get('phase_names') != ['movement', 'pickup']):
            raise ValueError('invalid decision outcome footer')
        table = footer.get('material_phase_counters')
        if not isinstance(table, list) or not 1 <= len(table) <= 65536: raise ValueError('invalid outcome material table')
        totals = {}
        for material, phases in enumerate(table):
            if not isinstance(phases, list) or len(phases) != 2: raise ValueError('invalid outcome phase table')
            for phase, values in enumerate(phases):
                if not isinstance(values, list) or len(values) != len(COUNTERS): raise ValueError('invalid outcome counter table')
                for value in values: natural(value, 'outcome counter')
                c = dict(zip(COUNTERS, values))
                if c['attempted']+c['not_attempted'] != c['eligible'] or any(v > c['eligible'] for v in values):
                    raise ValueError('inconsistent outcome counters')
                if c['no_candidate'] > c['attempted'] or c['cooldown_not_attempted']+c['missing_field_not_attempted'] > c['not_attempted']:
                    raise ValueError('inconsistent outcome attempt counters')
                if any(c):
                    totals[(material, ('movement', 'pickup')[phase])] = c
        return totals
