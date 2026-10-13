import json,math,statistics
from pathlib import Path
out=Path(__file__).resolve().parent
rows=[r for r in json.loads((out/'final-runs.json').read_text()) if r['timed']]
assert len(rows)==384,len(rows)
index={(r['fixture'],r['count'],r['pair'],r['variant']):r for r in rows}
def interval(v):
 m=statistics.mean(v);h=2.364624251*statistics.stdev(v)/math.sqrt(len(v));return {'mean':m,'ci95':[m-h,m+h],'pairs':v}
metrics={'cpu_tick_ms':lambda r:r['result']['benchmark_run_cpu_ns']/2048/1e6,
 'startup_inclusive_cpu_s':lambda r:r['total_process_cpu_seconds'],
 'setup_cpu_s':lambda r:r['result']['benchmark_setup_cpu_ns']/1e9,
 'run_wall_s':lambda r:r['result']['run_ns']/1e9,
 'tick_p95_ms':lambda r:r['result']['tick_p95_ns']/1e6}
results=[]
for b,c in [('before','master'),('before','fixed'),('master','fixed')]:
 cohort={k:[[] for _ in range(8)] for k in metrics};cells=[]
 for f in sorted({r['fixture'] for r in rows}):
  for count in (1,2,4,8):
   cell={'fixture':f,'count':count,'metrics':{}}
   for k,access in metrics.items():
    before=[access(index[f,count,p,b]) for p in range(8)];after=[access(index[f,count,p,c]) for p in range(8)]
    if min(before)<=0:continue
    changes=[100*(a/x-1) for a,x in zip(after,before)]
    cell['metrics'][k]={'before':statistics.mean(before),'after':statistics.mean(after),'change':interval(changes)}
    for p,x in enumerate(changes):cohort[k][p].append(x)
   cells.append(cell)
 results.append({'baseline':b,'candidate':c,'cohort':{k:interval([statistics.mean(x) for x in pairs]) for k,pairs in cohort.items() if all(pairs)},'cells':cells})
(out/'three-summary.json').write_text(json.dumps(results,indent=2)+'\n')
for r in results: print(r['baseline'],r['candidate'],json.dumps(r['cohort']))
