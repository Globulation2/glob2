"""Temporary reversible discovery screening; never reads held-out event payloads.

The first pass keeps compact global means. A second pass verifies only strong
empirical dominance against bounded witnesses; it does not select a shipping
policy. Retained captures permit later shortlist and drift re-scoring.
"""
import argparse
from collections import Counter
import gzip
import json
from pathlib import Path
import sqlite3
import time

from tools.resource_claim_offline import (GameAggregates, alias_views, deep_budget,
    file_hash, identity, score_logical_game, catalog_arrays, Scorer, NO_TILE)
from tools.resource_claim_deep import read_event, verify_pilot, MAX_RECORD, MAX_METADATA
from tools.tournaments.results import Results
from tools.tournaments.claim_capture import validate_manifest


LOCAL_REGIME_RULE = 'Toroidal Euclidean R16 disk around nearest published source: <=4 legal pickup goals is locally sparse; >4 legal and zero available goals is separately fully claimed. No global-density requirement. Fixed before pattern scoring.'


def local_goal_features(event, eligible, available_components):
    result={}
    for name,origin in (('unit',event.unit),('nearest',event.nearest),('midpoint',event.midpoint())):
        for radius in (16,32):
            key=f'{name}_r{radius}'
            if origin==NO_TILE:
                result[key]=None;continue
            ox,oy=origin%event.width,origin//event.width
            cells={((oy+y)%event.height)*event.width+(ox+x)%event.width
                   for y in range(-radius,radius+1) for x in range(-radius,radius+1) if x*x+y*y<=radius*radius}
            goals=[c for c in eligible if c.source in cells]
            raw=sum(c.source in cells for c in event.candidates.values())
            available=sum(c.component in available_components if c.component!=NO_TILE else c.available for c in goals)
            result[key]={'area_tiles':len(cells),'legal_goals':len(goals),'available_goals':available,
                         'unavailable_goals':len(goals)-available,'raw_material_sites':raw,'ineligible_material_sites':raw-len(goals)}
    nearest=result['nearest_r16']
    target=event.candidates.get(event.nearest);component=None
    if target is not None and target.component!=NO_TILE:
        group=[c for c in event.candidates.values() if c.component==target.component]
        legal=sum(c.access_eligible for c in group);stocks=sum(c.available for c in group)
        component={'component':target.component,'nearest_access_eligible':target.access_eligible,'material_sites':len(group),'legal_goals':legal,
                   'available_stock_sites':stocks,'available_goals':legal if stocks else 0}
    return {'nearest_pool_component':component,'local_goals':result,'local_sparse_nearest_r16':None if nearest is None else nearest['legal_goals']<=4,
            'local_fully_claimed_nearest_r16':None if nearest is None else nearest['legal_goals']>4 and nearest['available_goals']==0,
            'local_regime_rule':LOCAL_REGIME_RULE}


def realized_farming_regime(feature):
    if type(feature.get('tick')) is not int or feature['tick']<60000:return None
    component=feature.get('nearest_pool_component')
    if feature.get('family')=='pooled' and feature.get('pooled_farm') is True and isinstance(component,dict) and type(component.get('component')) is int and 0<=component['component']<NO_TILE and component.get('nearest_access_eligible') is True and component.get('available_stock_sites')==0:
        return 'late_exhausted_selected_pool'
    if feature.get('local_sparse_nearest_r16') is not True and feature.get('local_fully_claimed_nearest_r16') is not True:return None
    if feature.get('family')=='protected' and feature.get('material')==1 and (feature.get('protected_food_direct') is True or feature.get('protected_food_direct') is None and feature.get('protected_food_adjacency_proxy') is True):return 'late_sparse_protected'
    if feature.get('family')=='pooled' and feature.get('pooled_farm') is True:return 'late_sparse_pooled'
    return None


def missing_farming_regimes(required,realized):
    missing=[]
    if 'protected' in required and not realized.get('late_sparse_protected'):missing.append('late_sparse_protected')
    if 'pooled' in required and not (realized.get('late_sparse_pooled') or realized.get('late_exhausted_selected_pool')):missing.append('late_sparse_or_exhausted_selected_pool')
    return missing


def goal_features(event, metadata, labels):
    """Pool availability belongs to pickup accesses, not just physical stocks."""
    available_components={c.component for c in event.candidates.values() if c.available and c.component!=NO_TILE}
    eligible=[c for c in event.candidates.values() if c.access_eligible]
    available=sum(c.component in available_components if c.component!=NO_TILE else c.available for c in eligible)
    cells=event.width*event.height
    observed=metadata.get('observation_features') or metadata.get('features') or {}
    forbidden=observed.get('nearby_forbidden_count');growth=observed.get('resource_growth_rate')
    direct=observed.get('nearby_forbidden_growing_material_count')
    proxy=metadata.get('material')==1 and type(forbidden) is int and forbidden>0 and type(growth) is int and growth>0 and observed.get('farm_areas_enabled') is False
    return {**local_goal_features(event,eligible,available_components),'family':labels.get('farm_mode'),'material':metadata.get('material'),
        'nearest_octant':event.octant(),'tick':metadata.get('tick'),
        'reason':metadata.get('reason'),'nearby_forbidden_count':forbidden,
        'nearby_forbidden_material_count':observed.get('nearby_forbidden_material_count'),
        'nearby_forbidden_growing_material_count':direct,
        'protected_food_direct':None if direct is None else metadata.get('material')==1 and type(direct) is int and direct>0,
        'protected_food_adjacency_proxy':proxy,
        'protected_evidence_limit':'418 adjacency is proxy-only; direct growing requested-material count is configured regrowth potential, not habitat success or AI intent',
        'pooled_farm':observed.get('pooled_farm'),
        'exhausted_pool':observed.get('exhausted_pool'),'resource_type':observed.get('resource_type'),
        'yield_consumption':observed.get('yield_consumption'),'infinite':observed.get('infinite'),
        'raw_stock_count':len(event.candidates),'eligible_goal_count':len(eligible),
        'available_eligible_goal_count':available,
        'goal_density':'dense' if len(eligible)*128>=cells else 'sparse' if len(eligible)*1024<=cells else 'intermediate',
        'raw_stock_density':'dense' if len(event.candidates)*128>=cells else 'sparse' if len(event.candidates)*1024<=cells else 'intermediate'}


class DominanceScreen:
    """Bounded witnesses, one deep event/game, all observed niches respected."""
    def __init__(self, aliases, anchors, protected, split, *, min_games=16):
        import numpy as np
        if not 1<=len(anchors)<=64 or len(set(anchors))!=len(anchors):raise ValueError('bounded unique witnesses required')
        if any(not 0<=a<aliases*2 for a in anchors):raise ValueError('invalid witness')
        self.aliases=aliases;self.anchors=tuple(anchors);self.protected=set(protected)|set(anchors)
        self.viable=np.ones((len(anchors),aliases*2),dtype=bool)
        self.probes=np.zeros(aliases*2,dtype=np.uint64)
        self.split=split;self.min_games=min_games;self.required_families=set((split.get('strata') or {}).values()) or {'default','protected','pooled'};self.games=set();self.families=Counter();self.niches=Counter();self.niche_winners={};self.realized_regimes=Counter();self.event_regimes={}

    def add(self, game_id, matrix, features):
        import numpy as np
        if game_id in self.games or self.split['games'].get(game_id)!='discovery':raise ValueError('unique discovery games only; holdout forbidden')
        if matrix.shape!=(self.aliases,2) or np.any(matrix['events']!=1):raise ValueError('one frozen event per game required')
        if self.split.get('strata') and self.split['strata'].get(game_id)!=features.get('family'):raise ValueError('game family differs from frozen split')
        rows=matrix.reshape(-1)
        if np.any(self.probes>np.iinfo(np.uint64).max-rows['probes']):raise ValueError('probe counter overflow')
        self.probes+=rows['probes']
        fields={name:rows[name].copy() for name in ('fallback','access_unreachable','access_compared','oracle_missing','access_regret','probes')}
        for column,index in enumerate(self.anchors):
            anchor=rows[index]
            # Equal failure/reachability and known-cost coverage prevent an
            # absent travel label being interpreted as zero regret.
            good=(fields['fallback']==anchor['fallback']) & (fields['access_unreachable']==anchor['access_unreachable'])
            good&=(fields['access_compared']==anchor['access_compared']) & (fields['oracle_missing']==anchor['oracle_missing'])
            good&=(fields['access_regret']>=anchor['access_regret']) & (fields['probes']>=anchor['probes'])
            self.viable[column]&=good
        failure=rows['fallback']+rows['access_unreachable']+rows['oracle_missing']
        winner=int(np.lexsort((np.arange(len(rows)),rows['probes'],rows['access_regret'],failure))[0])
        self.protected.add(winner);self.niche_winners[game_id]=winner
        self.event_regimes[game_id]=features
        regime=realized_farming_regime(features)
        if regime:self.realized_regimes[regime]+=1
        self.games.add(game_id);self.families[features.get('family')]+=1
        self.niches[str((features.get('family'),features.get('material'),features.get('nearest_octant'),features.get('goal_density')))]+=1

    def report(self):
        import numpy as np
        missing_families={family:max(0,5-self.families[family]) for family in self.required_families if self.families[family]<5}
        missing_regimes=missing_farming_regimes(self.required_families,self.realized_regimes)
        ready=len(self.games)>=self.min_games and not missing_families and not missing_regimes
        removed=[]
        if ready:
            for candidate in range(self.aliases*2):
                if candidate in self.protected:continue
                for column,anchor in enumerate(self.anchors):
                    if self.viable[column,candidate] and int(self.probes[candidate])*4>=int(self.probes[anchor])*5 and self.probes[candidate]>self.probes[anchor]:
                        removed.append({'candidate':candidate,'witness':anchor,'rule':'same-failures-never-better-access-travel-or-probes-and-at-least25pct-more-total-probes',
                                        'candidate_probes':int(self.probes[candidate]),'witness_probes':int(self.probes[anchor])});break
        audit=[r['candidate'] for r in removed if int(identity(['claims-eliminated-audit-v1',r['candidate']])[:16],16)%10==0]
        return {'version':1,'ready_for_reversible_allocation':ready,'shipping_policy_selected':False,
            'discovery_games':sorted(self.games),'family_games':dict(self.families),'niches':dict(self.niches),
            'covered_families':[k for k,v in self.families.items() if v>=5],
            'required_families':sorted(self.required_families),'missing_family_games':missing_families,
            'realized_regimes':dict(self.realized_regimes),'missing_realized_regimes':missing_regimes,'event_regimes':self.event_regimes,
            'direct_protected_food_events':sum(f.get('protected_food_direct') is True for f in self.event_regimes.values()),
            'protected_proxy_only_events':sum(f.get('protected_food_direct') is None and f.get('protected_food_adjacency_proxy') is True for f in self.event_regimes.values()),
            'missing_direct_protection_backstop':'Legacy proxy-only coverage does not qualify final farming scenarios; retain factor/niche backstops and eliminated audit until direct coverage exists.',
            'regime_gate':LOCAL_REGIME_RULE+' Require late>=60000 and food material1 with direct requested-growing-material evidence or explicitly labeled legacy adjacency proxy for protected; observed pooling for pooled. Late exhausted selected pool (eligible nearest access and zero available physical sources in its frozen component) also qualifies independently of neighboring pools. Minimal screening safeguard, not final scenario sufficiency.',
            'elimination_scope':'global across planned families only when all family minima pass',
            'protected_candidates':sorted(self.protected),'observed_event_niche_winners':self.niche_winners,'witnesses':self.anchors,'eliminated':removed,
            'eliminated_audit_sample':audit,'audit_rule':'deterministic10percent sample; reopen after claim-policy distribution shifts',
            'surviving_candidates':self.aliases*2-len(removed),'total_candidates':self.aliases*2,
            'interpretation':'Reversible empirical allocation only. Missing niches retain factor backstops; no holdout or final winner inference.',
            'secondary_rule':'10percent travel-loss bootstrap rule not applied; strongest exact empirical dominance only.'}


def witnesses_and_backstops(catalog, aggregates):
    """Protect separate family×radius/stride/origin/direction factors, not their product."""
    import numpy as np
    sums=aggregates.sums[0];counts=aggregates.counts[0]
    means=np.divide(sums,counts,out=np.full(sums.shape,np.inf),where=counts>0).reshape(-1,len(aggregates.METRICS))
    def score(index):return (means[index,4],means[index,6],means[index,0],means[index,8],index)
    best={}
    db=sqlite3.connect(f'file:{Path(catalog).resolve()/"index.sqlite"}?mode=ro')
    try:
        for ordinal,encoded in db.execute('SELECT id,spec FROM aliases ORDER BY id'):
            spec=json.loads(encoded)
            for policy in range(2):
                index=ordinal*2+policy
                for field in ('radius','stride','origin','direction'):
                    key=(spec['family'],field,spec[field],policy)
                    if key not in best or score(index)<score(best[key]):best[key]=index
    finally:db.close()
    protected=set(best.values());anchors=[]
    # Global extremes include cheap fallback witnesses; factor representatives
    # and niche winners remain protected even when not used as witnesses.
    for metric in (8,0,4,6):
        for index in np.argsort(means[:,metric],kind='stable')[:8]:
            if int(index) not in anchors:anchors.append(int(index))
    for index in sorted(protected,key=score):
        if len(anchors)>=64:break
        if index not in anchors:anchors.append(index)
    return anchors,protected


def selected_observation(source,record,ordinal):
    """Join only the final reservoir row, never condition which event is selected."""
    total=0
    with source.open_artifact(record,'resource-claims-shadow.jsonl.gz','rb') as raw:
        with gzip.GzipFile(fileobj=raw,mode='rb') as stream:
            while True:
                line=stream.readline(1024*1024+1)
                if not line:break
                total+=len(line)
                if len(line)>1024*1024 or total>2*1024**3:raise ValueError('ordinary trace join exceeds bounded input allowance')
                row=json.loads(line)
                if row.get('kind')=='decision' and row.get('ordinal')==ordinal:
                    return {k:row.get(k) for k in ('ordinal','tick','reason','material','features')}
    raise ValueError('final deep ordinal absent from ordinary trace')


def discovery_inputs(results, split, directory):
    """Keep bounded immutable copies; do not open any held-out event artifact."""
    directory=Path(directory);directory.mkdir(parents=True,exist_ok=False)
    inputs=[];coverage=[]
    for record in list(Results(results)):
        game_id=record['job']['labels']['initial_state_identity'];role=split['games'][game_id]
        item={'game_id':game_id,'role':role,'category':record['category']}
        if role!='discovery' or record['category']!='success':coverage.append(item);continue
        source=Results(results)
        with source.open_artifact(record,'resource-claims-deep/manifest.json','rb') as f:data=f.read(MAX_METADATA+1)
        if len(data)>MAX_METADATA:raise ValueError('oversized capture manifest')
        manifest=json.loads(data);validate_manifest(manifest);item['eligible']=manifest['eligible']
        if not manifest['eligible']:coverage.append(item);continue
        context=record['job']['config']['resource_claims_deep_context']
        if manifest['context']!=context:raise ValueError('capture context differs from frozen job')
        dest=directory/game_id;dest.mkdir();(dest/'manifest.json').write_bytes(data)
        with source.open_artifact(record,'resource-claims-deep/event.bin','rb') as f:data=f.read(MAX_RECORD+1)
        if len(data)>MAX_RECORD:raise ValueError('oversized capture record')
        (dest/'event.bin').write_bytes(data)
        observation=selected_observation(source,record,manifest['ordinal'])
        (dest/'observation.json').write_text(json.dumps(observation,sort_keys=True)+'\n')
        inputs.append((dest,context,record['job']['labels']));coverage.append(item)
    return inputs,coverage


def benchmark_retained(event,catalog,views,report):
    removed={row['candidate'] for row in report['eliminated']}
    audit=set(report['eliminated_audit_sample'])
    aliases=[i for i in range(len(views)) if any(2*i+p not in removed or 2*i+p in audit for p in range(2))]
    wanted=set(int(v) for v in views[aliases,event.octant()])
    if not removed:return {'performed':False,'reason':'no elimination; retained catalog unchanged'}
    def measure(selected):
        scorer=Scorer(event,cache_bytes=0);batch=[];entries=arrays=0;batch_entries=0
        cpu=time.process_time();wall=time.monotonic()
        for index,(_,data) in enumerate(catalog_arrays(catalog)):
            if selected is not None and index not in selected:continue
            count=len(data)//5
            if batch and (len(batch)>=1024 or batch_entries+count>1048576):
                scorer.evaluate_many(batch);batch=[];batch_entries=0
            batch.append(data);batch_entries+=count;entries+=count;arrays+=1
        if batch:scorer.evaluate_many(batch)
        return {'process_cpu_seconds':time.process_time()-cpu,'wall_seconds':time.monotonic()-wall,'physical_arrays':arrays,'probe_entries':entries}
    full=measure(None);retained=measure(wanted)
    return {'performed':True,'game_id':event.game_id,'full':full,'retained_plus_audit':retained,
        'retained_logical_aliases':len(aliases),'process_cpu_reduction_fraction':1-retained['process_cpu_seconds']/full['process_cpu_seconds'],
        'scope':'same-event offline two-policy kernel; both scan/validate catalog; no shipping-policy timing claim; shared-host wall non-isolated'}


def run(results,catalog,split_path,output,score_cache=None):
    import shutil
    split=json.loads(Path(split_path).read_text())
    if identity({k:v for k,v in split.items() if k!='identity'})!=split.get('identity'):raise ValueError('frozen split digest mismatch')
    output=Path(output);output.mkdir(parents=True,exist_ok=False)
    state={'complete':False,'shipping_policy_selected':False,'split_identity':split['identity'],
        'tool_sha256':file_hash(__file__),'scorer_sha256':file_hash(Path(__file__).with_name('resource_claim_offline.py'))}
    (output/'status.json').write_text(json.dumps(state,indent=2)+'\n')
    catalog=Path(catalog);catalog_info=json.loads((catalog/'catalog.json').read_text())
    budget=deep_budget(catalog_bytes=sum(p.stat().st_size for p in catalog.rglob('*') if p.is_file()),events=len(split['games']),
        freeze_bytes=len(split['games'])*MAX_RECORD,free_bytes=shutil.disk_usage(output).free)
    inputs,coverage=discovery_inputs(results,split,output/'frozen')
    state['input_subset_identity']=identity([{'context':context,'wire_sha256':file_hash(path/'event.bin')} for path,context,_ in inputs])
    (output/'coverage.json').write_text(json.dumps({'games':coverage,'usable_discovery':len(inputs),'required_discovery':16,'pending_games':sorted(set(split['games'])-{r['game_id'] for r in coverage}),'budget':budget},indent=2)+'\n')
    families=Counter(labels.get('farm_mode') for _,_,labels in inputs)
    required=set((split.get('strata') or {}).values()) or {'default','protected','pooled'}
    missing_families={f:max(0,5-families[f]) for f in required if families[f]<5}
    if len(inputs)<16 or missing_families:
        state.update(complete=True,screening_ready=False,usable_discovery=len(inputs),family_games=dict(families),missing_family_games=missing_families,reason='requires16usable discovery and5per planned family; no candidate eliminated')
        (output/'status.json').write_text(json.dumps(state,indent=2)+'\n');return state
    observed_modes=set()
    def load(item):
        path,context,labels=item
        event,metadata=read_event(path/'event.bin',path/'manifest.json',expected_provenance={k:v for k,v in context.items() if k!='game_id'})
        observation_path=path/'observation.json'
        if observation_path.exists():
            observation=json.loads(observation_path.read_text())
            if observation['tick']!=metadata.get('tick') or observation['material']!=metadata.get('material'):raise ValueError('ordinary/deep observation mismatch')
            metadata=dict(metadata,observation_features=observation.get('features'),reason=observation.get('reason'))
        if event.game_id!=context['game_id'] or context['cohort_id']!=split['cohort_identity']:raise ValueError('capture game/cohort mismatch')
        if metadata.get('claims_enforced') is not labels['claims_enforced']:raise ValueError('capture claim mode differs from frozen cohort')
        observed_modes.add((metadata['claims_enforced'],metadata.get('active_policy'),metadata.get('policy_digest') or None))
        if len(observed_modes)>1:raise ValueError('mixed live policy configurations; screen separate frozen cohorts')
        return event,metadata,goal_features(event,metadata,labels)
    # Inspect the already selected events before the expensive full-catalog pass.
    features={};realized=Counter()
    for item in inputs:
        event,_,feature=load(item);features[event.game_id]=feature
        regime=realized_farming_regime(feature)
        if regime:realized[regime]+=1
    missing_regimes=missing_farming_regimes(required,realized)
    (output/'features.json').write_text(json.dumps(features,indent=2)+'\n')
    if missing_regimes:
        state.update(complete=True,screening_ready=False,usable_discovery=len(inputs),family_games=dict(families),
            realized_regimes=dict(realized),missing_realized_regimes=missing_regimes,
            reason='family labels do not establish late sparse farming; full-grid scoring deferred, no candidate eliminated')
        (output/'status.json').write_text(json.dumps(state,indent=2)+'\n');return state
    views=alias_views(catalog);aggregate=None;timings=[]
    def score(event,item):
        if score_cache is None:return score_logical_game([event],catalog,views),False,None
        from tools.resource_claim_score_cache import score_cached
        return score_cached(event,catalog,views,item[0]/'event.bin',score_cache)
    for item in inputs:
        event,metadata,feature=load(item);verify_pilot(event,metadata);features[event.game_id]=feature
        provenance=identity(event.provenance)
        if aggregate is None:aggregate=GameAggregates(catalog_info['aliases'],split,catalog_identity=catalog_info['identity'],provenance_identity=provenance)
        cpu=time.process_time();wall=time.monotonic()
        matrix,cache_hit,cache_key=score(event,item)
        aggregate.add(event.game_id,matrix,provenance_identity=provenance)
        timings.append({'game':event.game_id,'pass':'global','cache_hit':cache_hit,'cache_key':cache_key,'process_cpu_seconds':time.process_time()-cpu,'wall_seconds':time.monotonic()-wall})
    aggregate.write(output/'global')
    anchors,protected=witnesses_and_backstops(catalog,aggregate)
    del aggregate,matrix
    screen=DominanceScreen(catalog_info['aliases'],anchors,protected,split)
    for item in inputs:
        event,metadata,feature=load(item);cpu=time.process_time();wall=time.monotonic()
        matrix,cache_hit,cache_key=score(event,item);screen.add(event.game_id,matrix,feature)
        timings.append({'game':event.game_id,'pass':'dominance','cache_hit':cache_hit,'cache_key':cache_key,'process_cpu_seconds':time.process_time()-cpu,'wall_seconds':time.monotonic()-wall})
    report=screen.report();report['catalog_identity']=catalog_info['identity'];report['split_identity']=split['identity'];report['input_subset_identity']=state['input_subset_identity']
    event,_,_=load(inputs[0])
    report['cpu_measurement']=benchmark_retained(event,catalog,views,report)
    report['next_step']='Review survival reasons, preserved niches and measured CPU before changing subsequent campaign allocation; no final policy selected.'
    encoded=json.dumps(report,indent=2)+'\n'
    if len(encoded.encode())>256*1024**2 or shutil.disk_usage(output).free<len(encoded.encode())+8*1024**3:raise ValueError('screen report allowance or free-space reserve exceeded')
    (output/'screen.json').write_text(encoded)
    (output/'features.json').write_text(json.dumps(features,indent=2)+'\n')
    (output/'timings.json').write_text(json.dumps(timings,indent=2)+'\n')
    state.update(complete=True,screening_ready=report['ready_for_reversible_allocation'],eliminated=len(report['eliminated']),
                 source_mode='discovery only; holdout payloads never opened',elapsed_timings='wall; process_cpu_seconds measured separately')
    (output/'status.json').write_text(json.dumps(state,indent=2)+'\n');return state


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('results');parser.add_argument('catalog');parser.add_argument('split');parser.add_argument('output')
    parser.add_argument('--score-cache')
    args=parser.parse_args()
    print(json.dumps(run(args.results,args.catalog,args.split,args.output,args.score_cache),indent=2))
