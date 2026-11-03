"""Proposed reversible physical-work screening. No campaign dispatch or default application.

Inputs are paired discovery games, not independent decisions. Travel costs refer
exactly to the selected access. NaN means unavailable, never zero. Bootstrap
bounds support exploratory allocation only, not simultaneous winner inference.
"""
from dataclasses import asdict, dataclass
import hashlib
import json
from pathlib import Path


def prove_no_reachable_alternative(record, manifest, *, expected_provenance):
    """Bind an exhaustive v1 oracle's event-wide travel N/A to frozen bytes.

    G2CLMD01 v1 uses the complete static Dijkstra oracle, without a search
    budget. Unknown individual policy/access distances do not qualify.
    """
    from .tournaments.claim_capture import MAX_METADATA, read_capture
    if Path(manifest).stat().st_size>MAX_METADATA:raise ValueError('oversized deep manifest')
    manifest_bytes=Path(manifest).read_bytes()
    if len(manifest_bytes)>MAX_METADATA:raise ValueError('oversized deep manifest')
    attestation=json.loads(manifest_bytes)
    metadata, _, rows = read_capture(record, attestation, expected_provenance=expected_provenance)
    record_sha256=hashlib.sha256(Path(record).read_bytes()).hexdigest()
    if record_sha256!=attestation['file_sha256']:raise ValueError('deep record changed during proof')
    missing = (1 << 64)-1
    if (metadata.get('provenance', {}).get('censored') is not False or
        metadata.get('destination_lookup') != 'published-xy' or
        metadata.get('destination_xy_missing') is not False or
        metadata.get('optimum', 'absent') is not None or
        metadata.get('optimal', 'absent') is not None or
        type(metadata.get('oracle_expanded')) is not int or
        not 0 < metadata['oracle_expanded'] <= metadata['width']*metadata['height'] or
        metadata.get('source_count') != len(rows) or
        any(takeable and available and (distance != missing or access_distance != missing)
            for _, _, takeable, available, distance, access_distance in rows)):
        raise ValueError('event does not prove exhaustive absence of a reachable alternative')
    return {'version': 1, 'contract': 'G2CLMD01-v1-exhaustive-static-dijkstra',
            'game': metadata['game_id'], 'event': metadata['event_id'],
            'record_sha256': record_sha256,
            'manifest_sha256': hashlib.sha256(manifest_bytes).hexdigest(),
            'oracle_expanded': metadata['oracle_expanded'],
            'provenance': metadata['provenance']}


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
           candidate_arrays, physical_probe_lengths, *, coverage_ready=False, coverage_evidence=None,
           structural_na=(), thresholds=Thresholds()):
    """Return a proposed plan only; caller must explicitly approve any allocation.

    status[G,C,3] encodes fallback, unreachable-access, missing oracle labels.
    Matching status on every game is required; costs must be known on every
    travel-applicable game. Event-wide N/A needs a frozen exhaustive proof and
    retains all failure/probe guardrails. Witnesses and protected candidates must
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
    applicable=np.ones(g,dtype=bool);proofs=[]
    for proof in structural_na:
        if (not isinstance(proof,dict) or type(proof.get('version')) is not int or proof['version']!=1 or
            proof.get('contract')!='G2CLMD01-v1-exhaustive-static-dijkstra' or
            proof.get('game') not in game_ids or not isinstance(proof.get('event'),str) or not proof['event'] or
            any(not isinstance(proof.get(k),str) or len(proof[k])!=64 or
                any(c not in '0123456789abcdef' for c in proof[k])
                for k in ('record_sha256','manifest_sha256')) or
            type(proof.get('oracle_expanded')) is not int or not 0<proof['oracle_expanded']<=512*512 or
            not isinstance(proof.get('provenance'),dict) or proof['provenance'].get('censored') is not False or
            proof['provenance'].get('metadata_mode')!='published-xy' or
            any(not isinstance(proof['provenance'].get(k),str) or not proof['provenance'][k]
                for k in ('source_revision','bundle_id','cohort_id'))):
            raise ValueError('invalid structural travel N/A proof')
        row=game_ids.index(proof['game'])
        if not applicable[row] or not np.all(np.isnan(cost[row])) or not np.all(states[row,:,2]==1):
            raise ValueError('structural travel N/A contradicts matrix or duplicates an event')
        applicable[row]=False;proofs.append(proof)
    travel=cost if np.all(applicable) else cost[applicable]
    travel_families=np.asarray(families)[applicable]
    travel_games=[game for game,keep in zip(game_ids,applicable) if keep]
    travel_counts={f:int(np.count_nonzero(travel_families==f)) for f in needed}
    ready=bool(coverage_ready and g>=t.minimum_games and all(n>=t.minimum_family_games for n in counts.values())
               and len(travel_games)>=t.minimum_games and all(n>=t.minimum_family_games for n in travel_counts.values()))
    required=cost.nbytes+probe.nbytes+states.nbytes+(0 if travel is cost else travel.nbytes)+t.bootstrap_replicates*g*8+8*t.bootstrap_replicates*128+2*max(cost.nbytes,probe.nbytes,states.nbytes)
    if required>t.maximum_working_bytes:raise ValueError('screen working-set budget exceeded')
    def fingerprint(value):
        array=np.asarray(value)
        return {'shape':list(array.shape),'dtype':array.dtype.str,'sha256':hashlib.sha256(np.ascontiguousarray(array).tobytes()).hexdigest()}
    contract={'split_sha256':hashlib.sha256(json.dumps(split,sort_keys=True,separators=(',',':')).encode()).hexdigest(),'coverage_ready':coverage_ready,'coverage_evidence_sha256':hashlib.sha256(json.dumps(coverage_evidence,sort_keys=True,separators=(',',':')).encode()).hexdigest(),'inputs':{name:fingerprint(value) for name,value in (('costs',cost),('probes',probe),('status',states),('candidate_arrays',candidate_arrays),('physical_probe_lengths',physical_probe_lengths))},'estimand':'equal-game mean selected-access weighted travel; ratio of means, not mean regret ratio', 'thresholds':asdict(t),'games':list(game_ids),'families':list(families),'witnesses':anchors,'protected':sorted(protected)}
    contract.update(structural_na=proofs,travel_applicable_games=travel_games,
                    travel_family_games=travel_counts)
    digest=hashlib.sha256(json.dumps(contract,sort_keys=True,separators=(',',':')).encode()).hexdigest()
    # Bounded bootstrap weights reuse paired draws for every comparison and
    # preserve the observed family mix. No resampling of individual decisions.
    removed=[]
    if ready:
        rng=np.random.default_rng(t.seed);weights=np.zeros((t.bootstrap_replicates,len(travel_games)),dtype=np.float64)
        for family in sorted(needed):
            members=np.flatnonzero(travel_families==family)
            draws=rng.choice(members,(t.bootstrap_replicates,len(members)))
            for b,row in enumerate(draws):np.add.at(weights[b],row,1/len(travel_games))
        totals=[sum(map(int,probe[:,i])) for i in range(c)]
        anchors=sorted(anchors,key=lambda i:(totals[i],i));pending=[]
        def flush():
            if not pending:return
            delta=np.column_stack([travel[:,a]-t.travel_multiplier*travel[:,i] for i,a in pending])
            upper=np.quantile(weights@delta,t.confidence,axis=0,method='higher')
            for (i,a),bound in zip(pending,upper):
                if bound<=0:removed.append({'candidate':i,'witness':a,'paired_upper_travel_difference':float(bound),'candidate_probes':totals[i],'witness_probes':totals[a]})
            pending.clear()
        for i in range(c):
            if i in protected or not np.all(np.isfinite(travel[:,i])):continue
            for a in anchors:
                if totals[a]>(1-t.minimum_probe_savings)*totals[i] or totals[i]<=totals[a]:continue
                if not np.all(np.isfinite(travel[:,a])) or not np.array_equal(states[:,i],states[:,a]):continue
                # Do not average away a known family-level travel regression.
                if not any(np.mean(travel[travel_families==f,a]-t.travel_multiplier*travel[travel_families==f,i])>0 for f in needed):pending.append((i,a))
                break  # This witness owns the test even when its travel fails.
            if len(pending)>=128:flush()
        flush()
    plan=physical_plan(candidate_arrays,[r['candidate'] for r in removed],physical_probe_lengths,audit_percent=t.audit_percent)
    return {'version':2,'proposed_only':True,'applied':False,'shipping_policy_selected':False,'coverage_ready':ready,
            'contract':contract,'contract_sha256':digest,'family_games':counts,'eliminated':removed,'physical_work':plan,
            'uncertainty':'One-sided paired game bootstrap, stratified by frozen family; exploratory, not multiplicity-adjusted. One preordered matching witness per candidate.',
            'missing_coverage':'Mandatory factor/niche representatives and physical-array audit remain; final scenario coverage and human approval are separate.'}
