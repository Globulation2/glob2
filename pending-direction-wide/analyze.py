from pathlib import Path
import csv,gzip,json,collections,statistics
BASE=Path(__file__).parent
SUBSETS={
 'movement':lambda r:r['kind']==0,
 'footprint':lambda r:r['kind']==0 and r['route']==0,
 'combat':lambda r:r['kind']==0 and r['route']==2,
 'clearing':lambda r:r['kind']==0 and r['route']==1,
 'origin-open':lambda r:r['kind']==0 and r['here']>1,
 'origin-unreachable':lambda r:r['kind']==0 and r['here']==1,
 'origin-blocked':lambda r:r['kind']==0 and r['here']==0,
 'service-selection':lambda r:r['kind']==2 and r['medical'] in [1,2],
}
METRICS=['legal','blocked','stopped','progress','flat','worse','off_route','best_available','origin_blocked','route_exists','large_wrong_step','within_one_step_of_best']
rows=[];runs=[];worst=[]
for p in sorted((BASE/'runs').glob('*/run.json')):
 meta=json.loads(p.read_text())
 if not meta.get('complete'):continue
 w=meta['workload'];runs.append(meta);counters=collections.defaultdict(collections.Counter);seen=set()
 data=p.parent/'directions.csv.gz'
 if data.exists():
  for record in csv.DictReader(gzip.open(data,'rt')):
   r={k:int(v) for k,v in record.items()}
   if r['at_goal']:continue
   geometry=tuple(r[k] for k in ['variant','unit','unit_identity','building','building_identity','route','swim','x','y'])
   origin=r['here'] if r['here']>1 else r['best_legal_score'];available=r['best_legal_score']>=0;legal=bool(r['legal']);move=bool(r['dx'] or r['dy']);reach=r['next']>1
   features={'legal':legal,'blocked':move and not legal,'stopped':not move,'progress':legal and reach and origin>=0 and r['next']>origin,'flat':legal and reach and origin>=0 and r['next']==origin,'worse':legal and reach and origin>=0 and r['next']<origin,'off_route':legal and not reach,'best_available':legal and available and r['chosen_score']==r['best_legal_score'],'origin_blocked':r['here']==0,'route_exists':r['here']>1 or available,'large_wrong_step':legal and reach and origin>=0 and (origin-r['next'])>20,'within_one_step_of_best':legal and available and reach and r['best_legal_score']-r['chosen_score']<=10}
   if features['worse'] and r['kind']==0 and r['variant']==1:
    worst.append(dict(case=w['id'],remaining_distance_increase=(origin-r['next'])/10,**r))
    if len(worst)>200:worst=sorted(worst,key=lambda q:q['remaining_distance_increase'],reverse=True)[:40]
   for subset,accept in SUBSETS.items():
    if not accept(r):continue
    key=(subset,geometry);repeated=key in seen;seen.add(key)
    for dedup in [False,True]:
     if dedup and repeated:continue
     c=counters[(r['variant'],subset,dedup)];c['n']+=1
     for k,v in features.items():c[k]+=int(v)
 for variant in [0,1]:
  for subset in SUBSETS:
   for dedup in [False,True]:
    c=counters[(variant,subset,dedup)]
    rows.append(dict(case=w['id'],source=w['source'],generator=w.get('generator',''),width=w['width'],height=w['height'],teams=w['teams'],format=w['format'],players='/'.join(w['players']),ticks=meta['ticks'],variant=variant,subset=subset,dedup=dedup,n=c['n'],**{k:c[k] for k in METRICS}))
def aggregate(data):
 n=sum(r['n'] for r in data);eligible=[r for r in data if r['n']]
 return dict(progress_when_route_exists_pct=100*sum(r['progress'] for r in data)/sum(r['route_exists'] for r in data) if sum(r['route_exists'] for r in data) else None,cases=len(data),cases_with_requests=len(eligible),n=n,metrics={k:{'count':sum(r[k] for r in data),'pct':100*sum(r[k] for r in data)/n if n else None,'mean_case_pct':statistics.mean(100*r[k]/r['n'] for r in eligible) if eligible else None,'median_case_pct':statistics.median(100*r[k]/r['n'] for r in eligible) if eligible else None,'min_case_pct':min((100*r[k]/r['n'] for r in eligible),default=None),'max_case_pct':max((100*r[k]/r['n'] for r in eligible),default=None)} for k in METRICS})
groups=[]
for subset in SUBSETS:
 for v in [0,1]:
  for dedup in [False,True]:
   data=[r for r in rows if (r['subset'],r['variant'],r['dedup'])==(subset,v,dedup)]
   groups.append(dict(group='all',value='all',subset=subset,variant=v,dedup=dedup,**aggregate(data)))
   if dedup:continue
   for key in ['source','format','teams']:
    for value in sorted({r[key] for r in data},key=str):groups.append(dict(group=key,value=value,subset=subset,variant=v,dedup=False,**aggregate([r for r in data if r[key]==value])))
failures=[dict(case=p.parent.name,**json.loads(p.read_text())) for p in (BASE/'runs').glob('*/failure.json') if not (p.parent/'run.json').exists()]
summary=dict(completed=len(runs),expected=len(json.load(open(BASE/'workloads.json'))),ticks=sum(r['ticks'] for r in runs),pending_queries=sum(r['samples'] for r in runs),all_control_matches=all(r['probe_control_match'] for r in runs),cases_without_pending_movement=[r['case'] for r in rows if r['subset']=='movement' and r['variant']==1 and not r['dedup'] and r['n']==0],cases_without_pending_queries=[r['workload']['id'] for r in runs if r['samples']==0],failures=failures,groups=groups)
(BASE/'summary.json').write_text(json.dumps(summary,indent=2))
with (BASE/'per-case.csv').open('w') as f:
 writer=csv.DictWriter(f,fieldnames=list(rows[0]) if rows else ['case']);writer.writeheader();writer.writerows(rows)
(BASE/'worst-examples.json').write_text(json.dumps(sorted(worst,key=lambda r:r['remaining_distance_increase'],reverse=True)[:40],indent=2))
lines=['| Group | Cases with movement | Requests | Direct progress | Flyer-turn progress | Flyer-turn wrong way |','|---|---:|---:|---:|---:|---:|']
for key in ['all','source','format']:
 values=['all'] if key=='all' else sorted({g['value'] for g in groups if g['group']==key},key=str)
 for value in values:
  pair=[next(g for g in groups if (g['group'],g['value'],g['subset'],g['variant'],g['dedup'])==(key,value,'movement',v,False)) for v in [0,1]]
  if not pair[0]['n']:continue
  lines.append(f"| {value} | {pair[0]['cases_with_requests']} | {pair[0]['n']} | {pair[0]['metrics']['progress']['pct']:.2f}% | {pair[1]['metrics']['progress']['pct']:.2f}% | {pair[1]['metrics']['worse']['pct']:.2f}% |")
(BASE/'table.md').write_text('\n'.join(lines)+'\n')
print('Completed',len(runs),'ticks',summary['ticks'],'pending queries',summary['pending_queries'],'failures',len(failures));print('\n'.join(lines))
qualified=[r for r in rows if r['subset']=='movement' and r['variant']==1 and not r['dedup'] and r['n']>=100]
print('Lowest flyer-turn progress (>=100 requests):',[(r['case'],r['n'],round(100*r['progress']/r['n'],2),round(100*r['worse']/r['n'],2)) for r in sorted(qualified,key=lambda r:r['progress']/r['n'])[:12]])
