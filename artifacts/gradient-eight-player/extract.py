from pathlib import Path
import json,gzip,concurrent.futures
D=Path.cwd()/'artifacts/gradient-eight-player'
def read(path):
 econ={};measure={};perf={}
 for line in gzip.open(path/'stdout.log.gz','rt'):
  if line.startswith(('GLOB2_ECON ','GLOB2_MEASURE ')):
   x=dict(w.split('=',1) for w in line.split()[1:]);t=int(x['tick']);team=int(x['team']);dest=econ if line.startswith('GLOB2_ECON ') else measure
   if dest is econ:row={k:int(x[k]) for k in ['workers','warriors','explorers','foodCritical','needFood','inn','swarm']}
   else:row={k:int(v) for k,v in x.items() if k in ['meals','harvested_1'] or k.startswith('deaths_')}
   dest.setdefault(team,{})[t]=row
  elif line.startswith('GLOB2_PERF_FINAL ') and ' scope=' in line:
   import shlex
   x=dict(w.split('=',1) for w in shlex.split(line)[1:] if '=' in w)
   if x['scope']!='ai.player':perf[x['scope']]={k:x[k] for k in ['calls','total_ns','self_ns'] if k in x}
 return econ,measure,perf

def metrics(econ,measure,end,teams):
 out={k:0 for k in ['unit_ticks','critical_ticks','hungry_ticks','inn_ticks','meals','wheat','starved','other_deaths']}
 for team in teams:
  es=econ.get(team,{});ts=sorted(t for t in es if t<=end)
  for a,b in zip(ts,ts[1:]):
   pop=sum(es[a][k] for k in ['workers','warriors','explorers']);dt=b-a
   out['unit_ticks']+=pop*dt;out['critical_ticks']+=es[a]['foodCritical']*dt;out['hungry_ticks']+=es[a]['needFood']*dt;out['inn_ticks']+=es[a]['inn']*dt
  ms=measure.get(team,{});ts=sorted(t for t in ms if t<=end)
  if not ts:continue
  a,b=ms[ts[0]],ms[ts[-1]];delta=lambda k:b.get(k,0)-a.get(k,0)
  out['meals']+=delta('meals');out['wheat']+=delta('harvested_1');out['starved']+=sum(delta(f'deaths_{i}_1') for i in range(3));out['other_deaths']+=sum(delta(k) for k in b if k.startswith('deaths_') and not k.endswith('_1'))
 return out


def case(c):
 paths={v:D/'games'/c['id']/v for v in ['baseline','candidate']}
 if not all((p/'completed.json').exists() for p in paths.values()):
  failures={v:json.loads((p/'failure.json').read_text()) for v,p in paths.items() if (p/'failure.json').exists()}
  return {'case':c,'status':'failed' if failures else 'pending','failures':failures}
 results={v:json.loads((p/'result.json').read_text()) for v,p in paths.items()}
 end=min(r['ticks'] for r in results.values())//512*512
 row={'case':c,'status':'complete','end':end,'variants':{}}
 for v,p in paths.items():
  e,m,perf=read(p)
  row['variants'][v]={'common':metrics(e,m,end,range(8)),'ticks':results[v]['ticks'],'termination':results[v]['termination']}
  if end>=16384:row['variants'][v]['early']=metrics(e,m,16384,range(8))
 return row
cases=[c for c in json.loads((D/'cases.json').read_text()) if (D/'games'/c['id']).exists()]
with concurrent.futures.ThreadPoolExecutor(max_workers=2) as pool:rows=list(pool.map(case,cases))
(D/'extracted.json').write_text(json.dumps(rows))
print({status:sum(r['status']==status for r in rows) for status in ['complete','failed','pending']})
