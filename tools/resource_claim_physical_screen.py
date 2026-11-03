"""Proposed reversible physical-work screening. No campaign dispatch or default application.

Inputs are paired discovery games, not independent decisions. Travel costs refer
exactly to the selected access. NaN means unavailable, never zero. Bootstrap
bounds support exploratory allocation only, not simultaneous winner inference.
"""
from dataclasses import asdict, dataclass
import hashlib
import json


@dataclass(frozen=True)
class Thresholds:
    travel_multiplier: float = 1.10
    minimum_probe_savings: float = .25
    confidence: float = .95
    bootstrap_replicates: int = 2000
    seed: int = 172903
    minimum_games: int = 16
    minimum_family_games: int = 5
    audit_percent: int = 10
    maximum_working_bytes: int = 512 * 1024**2


def physical_plan(candidate_arrays, removed, physical_probe_lengths, *, audit_percent=10):
    """An array is dispensable only if no surviving alias/policy references it."""
    import numpy as np
    refs=np.asarray(candidate_arrays);lengths=np.asarray(physical_probe_lengths)
    if refs.ndim!=2 or refs.shape[1]!=8 or refs.dtype.kind!='u' or lengths.ndim!=1 or lengths.dtype.kind!='u':
        raise ValueError('unsigned candidate-by-eight directional references and array lengths required')
    if not len(lengths) or np.any(refs>=len(lengths)):raise ValueError('invalid physical array reference')
    if not 0<=audit_percent<=100:raise ValueError('invalid audit fraction')
    removed=set(removed)
    if any(type(i) is not int or not 0<=i<len(refs) for i in removed):raise ValueError('invalid removed candidate')
    active=np.ones(len(refs),dtype=bool);active[list(removed)]=False
    used=set(map(int,np.unique(refs)));retained=set(map(int,np.unique(refs[active])))
    dispensable=used-retained
    audit={i for i in dispensable if int(hashlib.sha256(f'claims-physical-audit-v1:{i}'.encode()).hexdigest()[:16],16)%100<audit_percent}
    evaluated=retained|audit
    before=sum(int(lengths[i]) for i in used);after=sum(int(lengths[i]) for i in evaluated)
    return {'retained_arrays':sorted(retained),'audit_arrays':sorted(audit),'omitted_arrays':sorted(dispensable-audit),
            'physical_arrays_before':len(used),'physical_arrays_after':len(evaluated),
            'physical_probe_entries_before':before,'physical_probe_entries_after':after,
            'projected_probe_entry_reduction':None if not before else 1-after/before,
            'measured_cpu_seconds':None,'cpu_interpretation':'Probe-entry savings are a work projection; native or scorer CPU requires an isolated measurement.'}


def screen(costs, probes, status, game_ids, families, split, witnesses, protected,
           candidate_arrays, physical_probe_lengths, *, coverage_ready=False, coverage_evidence=None, thresholds=Thresholds()):
    """Return a proposed plan only; caller must explicitly approve any allocation.

    status[G,C,3] encodes fallback, unreachable-access, missing oracle labels.
    Matching status on every game is required; costs must be known on all games
    before a candidate can be screened. Witnesses and protected candidates must
    be frozen before calling this routine. One cheapest status-matched witness
    is tested per candidate; failures do not trigger a search for favorable CIs.
    """
    import numpy as np
    t=thresholds
    game_ids=list(game_ids);families=list(families)
    if not (1<=t.travel_multiplier<=1.10 and  .25<=t.minimum_probe_savings<1 and .95<=t.confidence<1 and 200<=t.bootstrap_replicates<=10000):
        raise ValueError('unsupported screening thresholds')
    if t.minimum_games<16 or t.minimum_family_games<5:raise ValueError('minimum discovery coverage cannot be weakened')
    if coverage_ready and (not isinstance(coverage_evidence,dict) or coverage_evidence.get('ready_for_reversible_allocation') is not True):raise ValueError('attested coverage evidence required')
    cost=np.asarray(costs);probe=np.asarray(probes);states=np.asarray(status)
    if cost.ndim!=2 or probe.shape!=cost.shape or states.shape!=cost.shape+(3,):raise ValueError('paired array shapes differ')
    g,c=cost.shape
    if cost.dtype.kind!='f' or probe.dtype.kind!='u' or states.dtype.kind not in 'bu':raise ValueError('invalid metric types')
    if np.any(np.isinf(cost)) or np.any(cost[np.isfinite(cost)]<0) or np.any(states>1):raise ValueError('invalid metric values')
    if len(game_ids)!=g or len(set(game_ids))!=g or len(families)!=g:raise ValueError('unique paired games required')
    if any(split['games'].get(game)!='discovery' for game in game_ids):raise ValueError('holdout forbidden')
    if any(split.get('strata',{}).get(game)!=family for game,family in zip(game_ids,families)):raise ValueError('family differs from split')
    anchors=tuple(witnesses);protected=set(protected)|set(anchors)
    if not 1<=len(anchors)<=64 or len(set(anchors))!=len(anchors) or any(type(i) is not int or not 0<=i<c for i in protected):raise ValueError('bounded valid witnesses and representatives required')
    if len(candidate_arrays)!=c:raise ValueError('physical mapping differs')
    needed=set(split.get('strata',{}).values()) or set(families)
    counts={f:families.count(f) for f in needed}
    ready=bool(coverage_ready and g>=t.minimum_games and all(n>=t.minimum_family_games for n in counts.values()))
    required=cost.nbytes+probe.nbytes+states.nbytes+t.bootstrap_replicates*g*8+8*t.bootstrap_replicates*128+2*max(cost.nbytes,probe.nbytes,states.nbytes)
    if required>t.maximum_working_bytes:raise ValueError('screen working-set budget exceeded')
    def fingerprint(value):
        array=np.asarray(value)
        return {'shape':list(array.shape),'dtype':array.dtype.str,'sha256':hashlib.sha256(np.ascontiguousarray(array).tobytes()).hexdigest()}
    contract={'split_sha256':hashlib.sha256(json.dumps(split,sort_keys=True,separators=(',',':')).encode()).hexdigest(),'coverage_ready':coverage_ready,'coverage_evidence_sha256':hashlib.sha256(json.dumps(coverage_evidence,sort_keys=True,separators=(',',':')).encode()).hexdigest(),'inputs':{name:fingerprint(value) for name,value in (('costs',cost),('probes',probe),('status',states),('candidate_arrays',candidate_arrays),('physical_probe_lengths',physical_probe_lengths))},'estimand':'equal-game mean selected-access weighted travel; ratio of means, not mean regret ratio', 'thresholds':asdict(t),'games':list(game_ids),'families':list(families),'witnesses':anchors,'protected':sorted(protected)}
    digest=hashlib.sha256(json.dumps(contract,sort_keys=True,separators=(',',':')).encode()).hexdigest()
    # Bounded bootstrap weights reuse paired draws for every comparison and
    # preserve the observed family mix. No resampling of individual decisions.
    removed=[]
    if ready:
        rng=np.random.default_rng(t.seed);weights=np.zeros((t.bootstrap_replicates,g),dtype=np.float64)
        for family in sorted(needed):
            members=np.flatnonzero(np.asarray(families)==family)
            draws=rng.choice(members,(t.bootstrap_replicates,len(members)))
            for b,row in enumerate(draws):np.add.at(weights[b],row,1/g)
        totals=[sum(map(int,probe[:,i])) for i in range(c)]
        anchors=sorted(anchors,key=lambda i:(totals[i],i));pending=[]
        def flush():
            if not pending:return
            delta=np.column_stack([cost[:,a]-t.travel_multiplier*cost[:,i] for i,a in pending])
            upper=np.quantile(weights@delta,t.confidence,axis=0,method='higher')
            for (i,a),bound in zip(pending,upper):
                if bound<=0:removed.append({'candidate':i,'witness':a,'paired_upper_travel_difference':float(bound),'candidate_probes':totals[i],'witness_probes':totals[a]})
            pending.clear()
        for i in range(c):
            if i in protected or not np.all(np.isfinite(cost[:,i])):continue
            for a in anchors:
                if totals[a]>(1-t.minimum_probe_savings)*totals[i] or totals[i]<=totals[a]:continue
                if not np.all(np.isfinite(cost[:,a])) or not np.array_equal(states[:,i],states[:,a]):continue
                # Do not average away a known family-level travel regression.
                if not any(np.mean(cost[np.asarray(families)==f,a]-t.travel_multiplier*cost[np.asarray(families)==f,i])>0 for f in needed):pending.append((i,a))
                break  # This witness owns the test even when its travel fails.
            if len(pending)>=128:flush()
        flush()
    plan=physical_plan(candidate_arrays,[r['candidate'] for r in removed],physical_probe_lengths,audit_percent=t.audit_percent)
    return {'version':1,'proposed_only':True,'applied':False,'shipping_policy_selected':False,'coverage_ready':ready,
            'contract':contract,'contract_sha256':digest,'family_games':counts,'eliminated':removed,'physical_work':plan,
            'uncertainty':'One-sided paired game bootstrap, stratified by frozen family; exploratory, not multiplicity-adjusted. One preordered matching witness per candidate.',
            'missing_coverage':'Mandatory factor/niche representatives and physical-array audit remain; final scenario coverage and human approval are separate.'}
