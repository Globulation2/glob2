from pathlib import Path
import json,statistics as S,sys
sys.path.insert(0,'test')
from benchmark_resource_growth import interval
r=Path('artifacts/resource-growth/deeper');out={}
for kind in ['stages','layout-timing']:
 rows=[json.loads(l) for l in (r/kind/'measurements.jsonl').read_text().splitlines()];results={}
 for sc in sorted({x['scenario'] for x in rows}):
  a=[x for x in rows if x['scenario']==sc and x['repeat']>=0];vs={v:{x['repeat']:x for x in a if x['variant']==v} for v in {x['variant'] for x in a}};data={}
  def measures(x):
   d={'total_cpu_ms':x['result']['benchmark_run_cpu_ns']/1e6,'wall_ms':x['result']['run_ns']/1e6}
   if kind=='stages':
    for name,n in zip(['capture','old_growth','calculate','publish','submit','gradient_seed','gradient_propagate','ai'],x['stage_cpu']['cpu_ns']):d[name]=n/1e6
    d['residual']=d['total_cpu_ms']-sum(x['stage_cpu']['cpu_ns'])/1e6
   return d
  for v,rs in vs.items():data[v]={'median':{k:S.median(measures(x)[k] for x in rs.values()) for k in measures(next(iter(rs.values())))}}
  for before,after in ([('legacy','current'),('current','layout')] if kind=='stages' else [('current','layout')]):
   ds={k:[measures(vs[after][rep])[k]-measures(vs[before][rep])[k] for rep in vs[before]] for k in measures(next(iter(vs[before].values())))}
   data[after+'-vs-'+before]={'delta_ms':{k:{'mean':S.mean(v),'median':S.median(v),'ci':interval(v)} for k,v in ds.items()}}
   ratios=[vs[before][rep]['result']['run_ns']/vs[after][rep]['result']['run_ns']-1 for rep in vs[before]]
   data[after+'-vs-'+before]['throughput']={'median':S.median(ratios),'ci':interval(ratios)}
  results[sc]=data
 out[kind]=results
(r/'summary.json').write_text(json.dumps(out,indent=2))
print(json.dumps(out,indent=2))
