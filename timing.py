import json,subprocess,time,statistics,hashlib,platform
from pathlib import Path
root=Path(__file__).resolve().parents[2];out=Path(__file__).resolve().parent
rows=[]
for name,start,threads in [('small-castor',4096,2),('land-maxima',16384,4),('large-mixed',16384,4),('oazis-maxima',16384,4)]:
 for repeat in range(11):
  variants=['master','candidate'] if repeat%2==0 else ['candidate','master']
  for variant in variants:
   dest=out/f'time-{name}-{variant}-{repeat}';dest.mkdir()
   binary=out/('glob2-'+variant)
   command=[str(binary),'--run-game','--load-game',str(root/'artifacts/critical-path-prep'/(name+'-fixture')/'final.game.gz'),'--ticks',str(start+1024),'--compute-threads',str(threads),'--compute-experiments','ai','--benchmark-warmup','0','--output-dir',str(dest)]
   (dest/'command.json').write_text(json.dumps(command))
   with (dest/'engine.log').open('w') as log:subprocess.run(command,stdout=log,stderr=subprocess.STDOUT,check=True)
   result=json.loads((dest/'result.json').read_text());rows.append({'scenario':name,'variant':variant,'repeat':repeat,'result':result})
   (out/'timings.json').write_text(json.dumps(rows,indent=2))
  print(name,repeat,flush=True)
summary={}
for name in sorted({r['scenario'] for r in rows}):
 paired=[];cpu=[]
 for repeat in range(1,11):
  pair={r['variant']:r['result'] for r in rows if r['scenario']==name and r['repeat']==repeat}
  paired.append(100*(1-pair['candidate']['run_ns']/pair['master']['run_ns']))
  cpu.append(100*(pair['candidate']['benchmark_run_cpu_ns']/pair['master']['benchmark_run_cpu_ns']-1))
 summary[name]={'pairs':10,'ticks_per_window':1024,'paired_median_time_reduction_percent':statistics.median(paired),'paired_median_cpu_change_percent':statistics.median(cpu),'individual_reductions':paired}
(out/'timing-summary.json').write_text(json.dumps(summary,indent=2));print(json.dumps(summary),flush=True)
