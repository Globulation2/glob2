"""Optional owner-thread outcome linkage; samples remain clustered by game."""
from .claim_journeys import natural

COUNTERS = ('eligible','attempted','acquired','held_same','retarget_access','retarget_donor_same_access',
            'no_candidate','not_attempted','cooldown_not_attempted','missing_field_not_attempted','linked_rows','unlinked_held_exchange',
            'route_ready','route_pending','route_failed','route_arrived','fallback_wait','fallback_roam','fallback_release','cooldown_wait')

class Validator:
    def __init__(self, schema):
        self.version = schema.get('decision_outcome_version')
        self.limit = schema.get('max_samples')
        self.decisions = {}; self.outcomes = {}
        if self.version is not None:
            if type(self.version) is not int or self.version != 1:
                raise ValueError('unsupported decision outcome version')
            natural(self.limit, 'outcome sample bound', 2**32-1)
            if not self.limit: raise ValueError('empty outcome sample bound')
            # Wire allows Uint32 caps; bound actual retained rows independently.
            self.limit = min(self.limit, 1000000)

    def decision(self, row):
        if self.version is None: return
        if len(self.decisions) >= self.limit: raise ValueError('outcome decision bound exceeded')
        self.decisions[row['ordinal']] = {k: row.get(k) for k in ('owner','tick','material','phase','nearest','claims_enforced','pre_task')}

    def row(self, row):
        if self.version is None: raise ValueError('unadvertised decision outcome')
        if row.get('schema_version') != 1: raise ValueError('unsupported outcome row version')
        for key in ('ordinal','owner','pre_task','selected_task','final_task'):
            natural(row.get(key), 'outcome '+key)
        for key in ('tick','material'):
            natural(row.get(key), 'outcome '+key, 2**32-1)
        ordinal=row['ordinal']
        if not ordinal or ordinal in self.outcomes: raise ValueError('duplicate or zero outcome ordinal')
        if len(self.outcomes) >= self.limit: raise ValueError('outcome row bound exceeded')
        for key in ('pre_source','pre_access','selected_source','selected_access'):
            if row.get(key) is not None: natural(row[key], 'outcome '+key, 2**32-2)
        for prefix in ('pre','selected'):
            if (row.get(prefix+'_source') is None) != (row.get(prefix+'_access') is None):
                raise ValueError('partial outcome claim identity')
        for key in ('attempted','selection_succeeded','final_has_claim'):
            if type(row.get(key)) is not bool: raise ValueError('invalid outcome boolean')
        if row.get('phase') not in ('movement','pickup'): raise ValueError('invalid outcome phase')
        if row.get('route_result') not in ('not_attempted','ready','pending','failed','arrived'): raise ValueError('invalid outcome route')
        if row.get('fallback') not in ('none','wait','roam','release_job','gradient'): raise ValueError('invalid outcome fallback')
        if row.get('fallback_reason') not in ('none','no_resource','cooldown','path_failed'): raise ValueError('invalid outcome fallback reason')
        if (row['attempted'] and row.get('not_attempted_reason') is not None or
            not row['attempted'] and row.get('not_attempted_reason') not in ('held_claim','missing_field','cooldown','no_adjacent_candidate')):
            raise ValueError('invalid outcome attempt reason')
        selected=row.get('selected_access') is not None; held=row.get('pre_access') is not None
        change=('no_claim' if not selected else 'acquired' if not held else 'held_same' if row['selected_task']==row['pre_task'] else
                'retarget_donor_same_access' if row['selected_access']==row['pre_access'] else 'retarget_access')
        if row.get('claim_change') != change: raise ValueError('inconsistent outcome claim change')
        if (selected and type(row.get('selected_is_nearest_access')) is not bool or
            not selected and row.get('selected_is_nearest_access') is not None): raise ValueError('invalid outcome nearest classification')
        if row['selection_succeeded'] and (not row['attempted'] or not selected): raise ValueError('inconsistent selection success')
        self.outcomes[ordinal] = {k:row[k] for k in ('owner','tick','material','phase','pre_task','selected_access','selected_is_nearest_access')}

    def finish(self, summary):
        footer=summary.get('decision_outcomes')
        if self.version is None:
            if footer is not None: raise ValueError('unadvertised outcome footer')
            return
        if not isinstance(footer,dict) or footer.get('version') != 1 or tuple(footer.get('counter_names',())) != COUNTERS or footer.get('phase_names') != ['movement','pickup']:
            raise ValueError('invalid decision outcome footer')
        table=footer.get('material_phase_counters')
        if not isinstance(table,list) or not 1 <= len(table) <= 65536: raise ValueError('invalid outcome material table')
        linked={}
        for material, phases in enumerate(table):
            if not isinstance(phases,list) or len(phases)!=2: raise ValueError('invalid outcome phase table')
            for phase, values in enumerate(phases):
                if not isinstance(values,list) or len(values)!=len(COUNTERS): raise ValueError('invalid outcome counter table')
                for value in values: natural(value,'outcome counter')
                c=dict(zip(COUNTERS,values))
                if c['attempted']+c['not_attempted']!=c['eligible'] or any(v>c['eligible'] for v in values): raise ValueError('inconsistent outcome counters')
                if c['no_candidate']>c['attempted'] or c['cooldown_not_attempted']+c['missing_field_not_attempted']>c['not_attempted']: raise ValueError('inconsistent outcome attempt counters')
                linked[(material,('movement','pickup')[phase])]=c['linked_rows']
        actual={}
        for ordinal,out in self.outcomes.items():
            decision=self.decisions.get(ordinal)
            if decision is None or any(out[k]!=decision[k] for k in ('owner','tick','material','phase','pre_task')) or decision['claims_enforced'] is not True:
                raise ValueError('outcome does not match authoritative sampled decision')
            if out['selected_access'] is not None and out['selected_is_nearest_access'] != (out['selected_access']==decision['nearest']): raise ValueError('outcome nearest mismatch')
            key=(out['material'],out['phase']);actual[key]=actual.get(key,0)+1
        if footer.get('submitted_without_outcome')!=len(self.decisions)-len(self.outcomes): raise ValueError('outcome missing count mismatch')
        if footer.get('rows')!=len(self.outcomes) or any(actual.get(k,0)!=v for k,v in linked.items()) or any(k not in linked for k in actual):
            raise ValueError('outcome footer row count mismatch')
