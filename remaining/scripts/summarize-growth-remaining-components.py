from pathlib import Path
import json,sys,statistics
sys.path.insert(0,'test');from benchmark_resource_growth import interval
r=Path('artifacts/resource-growth/remaining/components')
def key(row):return '|'.join(str(row.get(k,'')) for k in ['stage','multi','size','pattern','retained'])
def samples(tag):
 out={}
 for row in json.load(open(r/(tag+'.json')))['samples']:
  if row['repeat']>=0:out.setdefault(key(row),{})[row['repeat']]=row
 return out
base=samples('control');summary={}
for tag in ['A','B','C','D','AB','ABCD']:
 data=samples(tag);results=summary[tag]={}
 for name,reps in data.items():
  d=results[name]={}
  for metric in ('elapsed_ns','cpu_ns','copied_bytes','copy_calls','allocations','cold_ns','cold_cpu_ns','cold_copy_calls','cold_bytes'):
   if metric not in next(iter(reps.values())):continue
   b=[base[name][i][metric] for i in sorted(reps)];v=[reps[i][metric] for i in sorted(reps)]
   d[metric]={'before':statistics.median(b),'after':statistics.median(v),'saved':statistics.median(x-y for x,y in zip(b,v))}
   if all(b):
    changes=[1-y/x for x,y in zip(b,v)];d[metric].update(reduction=statistics.median(changes),ci95=interval(changes))
(r/'summary.json').write_text(json.dumps(summary,indent=2))
for tag,cases in summary.items():
 print(tag)
 for name in ['stock|True|||','publication|True|||','capture||512|sparse|0','capture||512|clustered|0','capture||512|half|0','capture||512|dense|0']:
  d=cases[name];print(name,{m:{k:round(v*100,1) if k=='reduction' else v for k,v in d[m].items() if k in ('reduction','before','after')} for m in ('elapsed_ns','copied_bytes','copy_calls') if m in d})
