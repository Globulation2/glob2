import json,math,statistics
from pathlib import Path
p=Path(__file__).resolve().parent
f=p/'profile-measurements.jsonl'
rows=[json.loads(s) for s in f.read_text().splitlines()] if f.exists() else []
plan=json.loads((p/'profile-plan.json').read_text());out=[]
for c in plan['cases']:
 name=c['name'];r=[r for r in rows if r['case']==name];blocks=[]
 for i in range(5):
  b={r['variant']:r for r in r if r['repeat']==i}
  if 'previous' in b and 'api' in b:blocks.append(b)
 s={'case':name,'pairs':len(blocks)}
 if blocks:
  v=[math.log(b['api']['cpu_s']/b['previous']['cpu_s']) for b in blocks];m=statistics.mean(v)
  margin={2:12.706,3:4.303,4:3.182,5:2.776}.get(len(v),0)*statistics.stdev(v)/math.sqrt(len(v)) if len(v)>1 else None
  s.update(cpu_saved_percent=100*(1-math.exp(m)),ci95=None if margin is None else [100*(1-math.exp(m+margin)),100*(1-math.exp(m-margin))],pair_savings=[100*(1-math.exp(x)) for x in v])
  print(name,'API vs previous:',round(s['cpu_saved_percent'],2),'%', 'pairs',len(v),'CI',s['ci95'])
 ac=[b for b in blocks if all("'AC Power'" in b[v][k] for v in ['previous','api'] for k in ['before','after'])]
 if ac:
  logs=[math.log(b['api']['cpu_s']/b['previous']['cpu_s']) for b in ac];mean=statistics.mean(logs)
  margin={2:12.706,3:4.303,4:3.182,5:2.776}.get(len(logs),0)*statistics.stdev(logs)/math.sqrt(len(logs)) if len(logs)>1 else None
  s['ac_only']={'pairs':len(logs),'saving_percent':100*(1-math.exp(mean)),'ci95':None if margin is None else [100*(1-math.exp(mean+margin)),100*(1-math.exp(mean-margin))]}
 battery=[b for b in blocks if all("'Battery Power'" in b[v][k] for v in ['previous','api'] for k in ['before','after'])]
 if battery:
  logs=[math.log(b['api']['cpu_s']/b['previous']['cpu_s']) for b in battery];mean=statistics.mean(logs)
  margin={2:12.706,3:4.303,4:3.182,5:2.776}.get(len(logs),0)*statistics.stdev(logs)/math.sqrt(len(logs)) if len(logs)>1 else None
  s['battery_only']={'pairs':len(logs),'saving_percent':100*(1-math.exp(mean)),'ci95':None if margin is None else [100*(1-math.exp(mean+margin)),100*(1-math.exp(mean-margin))]}
 prof=[r for r in r if r['variant']=='profile']
 if prof:
  s['profile_runs']=len(prof);s['profile_mean']={k:statistics.mean(r['profile'][k] for r in prof) for k in prof[0]['profile']}
  m=s['profile_mean'];total=sum(m[k] for k in ['init_ns','setup_ns','seeds_ns','weighted_seeds_ns','propagation_ns'])
  s['phase_percent']={k:100*m[k]/total for k in ['init_ns','setup_ns','seeds_ns','weighted_seeds_ns','propagation_ns']}
  s['growth_percent_of_building']=100*m['growth_ns']/total
  s['building_seconds']=total/1e9
  s['profile_cpu_mean']=statistics.mean(r['cpu_s'] for r in prof)
  s['profile_wall_mean']=statistics.mean(r['wall_s'] for r in prof)
  s['deterministic_work_counts']=all(all(r['profile'][k]==prof[0]['profile'][k] for k in ['builds','weighted_builds','cells','weighted_cells','pushes','growths','capacity_bytes_added','pops','stale','layers']) for r in prof)
  print('  phases', {k:round(v,2) for k,v in s['phase_percent'].items()},'growth subset',round(s['growth_percent_of_building'],2),'%')
 out.append(s)
(p/'profile-analysis.json').write_text(json.dumps(out,indent=2)+'\n')
print(len(rows),'measured runs')
