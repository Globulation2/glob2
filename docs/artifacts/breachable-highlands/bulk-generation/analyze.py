"""Audit accepted logical samples through the shared, offline Results reader.

Successful maps get both production validation and these independent report checks.
Thresholds match the established start-access floors, rather than treating high
abundance saturation or a solo game's absent rival as defects. Raw observations
remain in the retained result records; this summary does not average away bad teams.
"""
import argparse
import csv
import hashlib
from collections import Counter, defaultdict
import json
import math
from pathlib import Path
from statistics import mean
from tools.tournaments.results import Results

p=argparse.ArgumentParser();p.add_argument('results');p.add_argument('output');p.add_argument('--support-table');a=p.parse_args()
s=Results(a.results)
cohorts=defaultdict(Counter);rejections=Counter();issues=[];metrics=defaultdict(list)
fallbacks=Counter();missing=0;incomplete=0;successes=0;accepted=0
pairs=defaultdict(list);coverage=defaultdict(Counter);success_coverage=defaultdict(Counter)
rows=[]
worlds=Counter();successful_worlds=set();report_hashes={};repeat_mismatches=[]
def metric(key,value,r):
    if value is not None:
        assert isinstance(value,(int,float)) and math.isfinite(value),(key,value)
        metrics[key].append((value,r['job']['id']))
def flag(r,reason):
    issues.append({'job':r['job']['id'],'reason':reason})
for r in s:
    accepted+=1;j=r['job'];params=j['config']['params'];cohort=j['labels']['variant'];res=r.get('result') or {}
    world=json.dumps([params,j['seeds']['map']],sort_keys=True)
    worlds[world]+=1
    cohorts[cohort][r['category']]+=1
    diag=res.get('diagnostic') or str(r.get('diagnostics'))
    # Drop only the seed-bearing service prefix when grouping diagnostic text.
    reason=diag.split(']: ',1)[-1]
    if r['category']!='success': rejections[(r['category'],reason)]+=1
    if cohort=='topology':
        pairs[tuple(params[k] for k in ('width','height','teams','valley-size'))].append((r['category'],reason))
    for k,v in params.items():coverage[k][v]+=1
    row={'job':j['id'],'cohort':cohort,'seed':j['seeds']['map'],'params':params,'category':r['category'],'diagnostic':diag}
    rows.append(row)
    if r['category']!='success':continue
    successes+=1
    successful_worlds.add(world)
    for k,v in params.items():success_coverage[k][v]+=1
    m=res.get('map_report');st=res.get('statistics')
    if not m or not st:
        missing+=1;flag(r,'Successful result missing its map report or statistics');continue
    report_hash=hashlib.sha256(json.dumps(m,sort_keys=True).encode()).hexdigest()
    if world in report_hashes and report_hashes[world]!=report_hash:repeat_mismatches.append(j['id'])
    report_hashes[world]=report_hash
    t=m.get('generation',{}).get('telemetry') or {}
    if not t or not t.get('enabled'):missing+=1
    if t.get('dropped_records') or t.get('invalid_values'):incomplete+=1
    telemetry=defaultdict(dict)
    for event in t.get('records',[]):
        telemetry[event['key']][event['subject']]=event['value']
    fall={e['key'] for e in t.get('records',[]) if e['kind']=='fallback'}
    fallbacks.update(fall)
    for key in ('breachable.saddles.actual','breachable.valleys.actual'):
        for value in telemetry[key].values():metric(key,value,r)
    detours=list(telemetry['breachable.saddle.detour-edges'].values())
    if detours:
        metric('minimum_saddle_detour_edges',min(detours),r)
        metric('maximum_saddle_detour_edges',max(detours),r)
    for resource,base,guarantee in [('wheat',84,54),('wood',32,24)]:
        requested=telemetry['breachable.farm.'+resource+'-requested'];placed=telemetry['breachable.farm.'+resource+'-tiles']
        ambient=(base*params[resource+'-amount']+50)//100
        home_counts=[]
        for cell,count in requested.items():
            if count==ambient+guarantee:
                actual=placed[cell];home_counts.append(actual)
                if actual<guarantee:flag(r,f'Home {cell} has {actual} {resource} tiles below guarantee {guarantee}')
        if len(home_counts)!=params['teams']:flag(r,'Incorrect number of home farm records')
        if home_counts:
            metric('minimum_home_'+resource+'_tiles',min(home_counts),r)
            metric('maximum_home_'+resource+'_tiles',max(home_counts),r)
    if st['viable_teams']!=params['teams']:flag(r,'Not all teams meet the measured viability floor')
    if m['map']['player_slots']!=params['teams']:flag(r,'Wrong team count')
    if any(c['units']['workers']!=params['workers'] for c in m['map']['colonies']):flag(r,'Wrong worker count')
    if not telemetry['breachable.saddles.actual'] or min(telemetry['breachable.saddles.actual'].values())<1:flag(r,'Missing saddle')
    for key in ('min_local_fit4','worst_wheat_distance','worst_wood_distance'):
        metric(key,st[key],r)
    metric('generation_seconds',res['seconds'],r)
    metric('process_seconds',r['seconds'],r)
    metric('peak_rss_bytes',r['resource_usage']['peak_rss_bytes'],r)
    metric('fairness',m['canonical_quality']['fairness'],r)
    walk=m['movement']['walking']
    pair_costs=[]
    for i,line in enumerate(walk['between_colonies']):
        for k,value in enumerate(line):
            if i>=k:continue
            if value is None:flag(r,'Disconnected colony pair')
            else:pair_costs.append(value)
    nearest_rivals=[min(value for k,value in enumerate(line) if i!=k and value is not None)
        for i,line in enumerate(walk['between_colonies'])
        if any(i!=k and value is not None for k,value in enumerate(line))]
    if nearest_rivals:metric('worst_nearest_rival_walk_cost',max(nearest_rivals),r)
    if pair_costs:
        metric('minimum_colony_pair_walk_cost',min(pair_costs),r)
        metric('maximum_colony_pair_walk_cost',max(pair_costs),r)
    for c in walk['colonies']:
        for resource,limit in [('wheat',24),('wood',32)]:
            cost=c['resources'][resource]['nearest_gather_cost']
            if cost is None or cost>limit:flag(r,f'{resource} access outside floor: {cost}')
    row.update(statistics=st,fairness=m['canonical_quality']['fairness'],seconds=res['seconds'],fallbacks=sorted(fall))

def summarize(values):
    values=sorted(values);n=len(values)
    def quantile(p):
        i=(n-1)*p;l=int(i);h=min(l+1,n-1)
        return values[l][0]+(values[h][0]-values[l][0])*(i-l)
    return {'count':n,'min':values[0][0],'p50':quantile(.5),'p95':quantile(.95),'p99':quantile(.99),
        'max':values[-1][0],'mean':mean(v for v,_ in values),
        'minimum_job':values[0][1],'maximum_job':values[-1][1]}
expected_reasons={
    'Breachable highlands needs both sides at least 128 tiles long.',
    'Leave at least two expansion valleys and two valleys along each axis; use a larger map, smaller valleys or fewer colonies.',
    'These valleys cannot fit separate home exits; use fewer colonies or a larger map.',
    'No additional ridge connects expansion valleys; use fewer colonies or a larger map.',
}
expected_count=sum(v for (category,reason),v in rejections.items() if category=='invalid_request' and reason in expected_reasons)
# Compare varied settings against the independently swept topology envelope.
# This catches a supported topology failing only after another slider changes,
# even if its diagnostic happens to resemble an anticipated layout rejection.
support={key:values[0][0]=='success' for key,values in pairs.items()
    if len(values)==2 and len({value[0] for value in values})==1}
if a.support_table:
    for row in csv.DictReader(open(a.support_table)):
        if row['completed_colony_settings']!='12':continue
        counts=set(map(int,row['supported_colony_counts_both_seeds'].split()))
        w=int(row['width_tiles']).bit_length()-1;h=int(row['height_tiles']).bit_length()-1
        for team in range(1,13):support[w,h,team,int(row['valley_size'])]=team in counts
envelope_checks=0;envelope_disagreements=[]
for row in rows:
    if row['cohort']=='topology':continue
    key=tuple(row['params'][k] for k in ('width','height','teams','valley-size'))
    if key not in support:continue
    envelope_checks+=1
    if (row['category']=='success')!=support[key]:envelope_disagreements.append(row['job'])
summary={'non_topology_envelope_checks':envelope_checks,'envelope_disagreements':envelope_disagreements,
    'unique_requested_worlds':len(worlds),'unique_successful_worlds':len(successful_worlds),
    'repeated_report_mismatches':repeat_mismatches,'expected_unsupported':expected_count,'unexpected_non_success':accepted-successes-expected_count,
    'metric_unit':'One value per accepted successful map; repeated subjects reduced to within-map extrema.',
    'planned':len(s.manifest['jobs']),'accepted':accepted,'successful':successes,
    'cohorts':dict(cohorts),'non_success_diagnostics':[{'category':k[0],'reason':k[1],'count':v} for k,v in rejections.most_common()],
    'successful_maps_missing_telemetry':missing,'successful_maps_incomplete_telemetry':incomplete,
    'fallback_maps':dict(fallbacks),'issues':issues,
    'seed_dependent_topology_outcomes':[{'topology':k,'outcomes':v} for k,v in pairs.items() if len({x[0] for x in v})>1],
    'metrics':{k:summarize(v) for k,v in metrics.items()},
    'requested_control_coverage':dict(coverage),'successful_control_coverage':dict(success_coverage)}
out=Path(a.output);out.mkdir(parents=True,exist_ok=True)
(out/'summary.json').write_text(json.dumps(summary,indent=2)+'\n')
with (out/'topology.csv').open('w',newline='') as f:
    writer=csv.writer(f);writer.writerow(['width_tiles','height_tiles','valley_size','supported_colony_counts_both_seeds','completed_colony_settings'])
    for w in range(6,10):
        for h in range(6,10):
            for v in (64,72,80,88,96):
                complete=[t for t in range(1,13) if len(pairs[(w,h,t,v)])==2]
                supported=[t for t in complete if all(x[0]=='success' for x in pairs[(w,h,t,v)])]
                writer.writerow([2**w,2**h,v,' '.join(map(str,supported)),len(complete)])
(out/'samples.json').write_text(json.dumps(rows,indent=2)+'\n')
print(json.dumps({k:v for k,v in summary.items() if k not in ('metrics','requested_control_coverage','successful_control_coverage')},indent=2))
