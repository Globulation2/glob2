from pathlib import Path
import json,sys,statistics
import numpy as np
def interval(values):
 a=np.asarray(values);rng=np.random.default_rng(713);samples=np.median(a[rng.integers(0,len(a),size=(100000,len(a)))],axis=1);return np.quantile(samples,[.025,.975]).tolist()
base=Path('artifacts/resource-growth/final-master');out=base/sys.argv[1];rows=[json.loads(l) for l in (out/'measurements.jsonl').read_text().splitlines()];plan=json.load(open(out/'metadata.json'))['plan'];summary={};decisions={}
def stat(values):return {'median':statistics.median(values),'ci95':interval(values)}
for case in plan['cases']:
 results=summary[case['id']]={}
 for scenario in case['scenarios']:
  pairs={}
  for row in rows:
   if row['case']==case['id'] and row['scenario']==scenario and row['repeat']>=0:pairs.setdefault(row['repeat'],{})[row['variant']]=row
  pairs=[p for p in pairs.values() if len(p)==2]
  if len(pairs)<10:continue
  d={'pairs':len(pairs)};results[scenario]=d
  d['tps_gain']=stat([p['reference']['result']['run_ns']/p['candidate']['result']['run_ns']-1 for p in pairs])
  d['cpu_reduction']=stat([1-p['candidate']['result']['benchmark_run_cpu_ns']/p['reference']['result']['benchmark_run_cpu_ns'] for p in pairs])
  for key,field in [('saved_ms','run_ns'),('cpu_saved_ms','benchmark_run_cpu_ns')]:d[key]=stat([(p['reference']['result'][field]-p['candidate']['result'][field])/1e6 for p in pairs])
  d['process_cpu_reduction']=stat([1-p['candidate']['cpu_s']/p['reference']['cpu_s'] for p in pairs])
  d['process_wall_gain']=stat([p['reference']['wall_s']/p['candidate']['wall_s']-1 for p in pairs])
  d['rss_change']=stat([p['candidate']['peak_rss_bytes']/p['reference']['peak_rss_bytes']-1 for p in pairs])
  d['p99_change']=stat([p['candidate']['result']['tick_p99_ns']/p['reference']['result']['tick_p99_ns']-1 for p in pairs]) if all('tick_p99_ns' in p['reference']['result'] and 'tick_p99_ns' in p['candidate']['result'] for p in pairs) else None
  d['tick_percentiles_ns']={key:{role:stat([p[role]['result'][key] for p in pairs]) for role in ['reference','candidate']} for key in ['tick_p50_ns','tick_p95_ns','tick_p99_ns'] if all(key in p[role]['result'] for p in pairs for role in ['reference','candidate'])}
  d['stages_saved_ms']={}
  for field in ['growth_computeNs','growth_publicationNs','growth_queueNs','growth_waitNs']:
   if all(field in p['reference']['result'] and field in p['candidate']['result'] for p in pairs):d['stages_saved_ms'][field]=stat([(p['reference']['result'][field]-p['candidate']['result'][field])/1e6 for p in pairs])
  for field in ['extraction_ns','preparation_ns','bytes_copied','allocations']:
   if all(field in p['reference']['result'].get('ai_pipeline',{}) and field in p['candidate']['result'].get('ai_pipeline',{}) for p in pairs):
    d[field+'_difference']=stat([p['candidate']['result']['ai_pipeline'][field]-p['reference']['result']['ai_pipeline'][field] for p in pairs])
 primary=['dense','multi','ai512','disabled512']
 wins=[(s,k) for s,d in results.items() for k in ['tps_gain','cpu_reduction'] if d[k]['median']>=.01 and d[k]['ci95'][0]>0]
 blockers=[(s,k,results[s]['pairs']) for s in primary if s in results for k in ['tps_gain','cpu_reduction'] if results[s][k]['ci95'][0]<=-.02]
 safe=all(s in results for s in primary) and not blockers
 regressions=[(s,k) for s,d in results.items() for k in ['tps_gain','cpu_reduction'] if d[k]['ci95'][1]<-.02]
 investigations=[(s,k) for s,d in results.items() for k in ['p99_change','rss_change'] if d[k] and d[k]['median']>.05]
 decisions[case['id']]={'same_behavior':case['same_behavior'],'wins':wins,'rules_out_primary_regressions':safe,'blockers':blockers,'demonstrated_regressions':regressions,'investigate':investigations,'qualifies_numerically':bool(wins) and safe and not regressions}
(out/'summary.json').write_text(json.dumps({'results':summary,'decisions':decisions},indent=2))
for c,rs in summary.items():
 print(c)
 for s,d in rs.items():print(' ',s,d['pairs'],'TPS',*[round(x*100,2) for x in [d['tps_gain']['median'],*d['tps_gain']['ci95']]],'CPU',*[round(x*100,2) for x in [d['cpu_reduction']['median'],*d['cpu_reduction']['ci95']]])
print(json.dumps(decisions,indent=2))
