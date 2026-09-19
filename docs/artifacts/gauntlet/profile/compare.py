import hashlib,json,os,statistics,subprocess,time
from pathlib import Path
base=Path('/tmp/gauntlet-evidence'); out=base/'profile-comparison'
bins={s:base/f'glob2-{s}-profile' for s in ['before','after']}
rows=[]
for size in [256,512]:
 for teams in [4,8,12]:
  for seed in range(1,13):
   reports={}; maps={}; row={'size':size,'teams':teams,'seed':seed,'runs':{}}
   for label,exe in bins.items():
    report=out/'current.json'; save=out/'current.map'
    for p in [report,save]: p.unlink(missing_ok=True)
    cmd=[str(exe),'--generate-map','gauntlet','--width',str(size),'--height',str(size),'--teams',str(teams),'--seed',str(seed),'--json',str(report),'--output',str(save)]
    start=time.perf_counter(); r=subprocess.run(cmd,capture_output=True,text=True); elapsed=time.perf_counter()-start
    row['runs'][label]={'exit':r.returncode,'seconds':elapsed,'stdout':r.stdout,'stderr':r.stderr}
    if report.exists():
     reports[label]=json.loads(report.read_text()); (out/f'{size}-{teams}-{seed}-{label}.json').write_text(report.read_text())
    if save.exists(): maps[label]=hashlib.sha256(save.read_bytes()).hexdigest()
   row['reports_equal']=reports.get('before')==reports.get('after'); row['map_hashes']=maps; row['maps_equal']=maps.get('before')==maps.get('after')
   rows.append(row); print(size,teams,seed,row['reports_equal'],row['maps_equal'],flush=True)
   (out/'comparison.json').write_text(json.dumps(rows,indent=2))
bench=[]
for mode in ['map-only','map-and-json']:
 for rep in range(7):
  for label in (['before','after'] if rep%2==0 else ['after','before']):
   cmd=[str(bins[label]),'--generate-map','gauntlet','--width','512','--height','512','--teams','12','--seed','1','--output',str(out/'bench.map')]
   if mode=='map-and-json': cmd+=['--json',str(out/'bench.json')]
   start=time.perf_counter(); r=subprocess.run(cmd,capture_output=True,text=True); elapsed=time.perf_counter()-start
   bench.append({'mode':mode,'rep':rep,'binary':label,'seconds':elapsed,'exit':r.returncode})
summary={'cases':len(rows),'successes':sum(x['runs']['before']['exit']==0 for x in rows),'reports_equal':all(x['reports_equal'] for x in rows),'maps_equal':all(x['maps_equal'] for x in rows),'benchmark':{}}
for mode in ['map-only','map-and-json']:
 summary['benchmark'][mode]={label:statistics.median(x['seconds'] for x in bench if x['mode']==mode and x['binary']==label) for label in bins}
(out/'benchmark.json').write_text(json.dumps(bench,indent=2)); (out/'summary.json').write_text(json.dumps(summary,indent=2)); print(json.dumps(summary,indent=2))
