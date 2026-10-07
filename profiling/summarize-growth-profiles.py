import json,re,statistics as S,collections
from pathlib import Path
r=Path('artifacts/resource-growth/profiling');out={}
def group(sym):
 if sym.startswith(('SimulationSnapshot::','auto SimulationSnapshot::')):return 'snapshot machinery'
 if any(x in sym for x in ['memcpy','memmove']):return 'libc copying (all callers)'
 if 'ResourceGrowth::' in sym:return 'new growth kernel/pipeline'
 if any(x in sym for x in ['Map::growResources','Fertility::applyGrowthOpportunities','Map::resourceGrowthRateAt','Fertility::GrowthCache::rate']):return 'legacy growth/ecology helpers'
 if 'Map::recordNaturalGrowth' in sym:return 'growth statistics'
 if 'Script::Observations::' in sym:return 'JavaScript observations'
 if any(x in sym for x in ['gradient_preparation::','gradient_kernel::']):return 'gradient preparation/propagation'
 if any(x in sym for x in ['ComputeExecutor::','pthread_mutex','futex','schedule','__switch_to']):return 'executor/locking/scheduling'
 return 'other'
for sc in ['dense','multi','ai512','disabled512']:
 out[sc]={}
 for mode in ['legacy','shared']:
  vals=[]
  for d in sorted((r/'gated'/sc).glob('*/'+mode)):
   if not (d/'flat.txt').exists():continue
   counters={}
   for line in (d/'counters.csv').read_text().splitlines():
    p=line.split(',')
    if len(p)>3:
     try:counters[p[2]]=float(p[0])
     except ValueError:pass
   text=(d/'flat.txt').read_text();groups=collections.defaultdict(float);symbols={}
   for line in text.splitlines():
    m=re.match(r'\s*([\d.]+)%\s+\S+\s+\S+\s+\[[.k]\] (.*)',line)
    if m:
     pct=float(m[1]);sym=m[2];groups[group(sym)]+=pct;symbols[sym]=symbols.get(sym,0)+pct
   vals.append({'path':str(d),'counters':counters,'groups':dict(groups),'symbols':symbols,'lost_samples':re.search(r'Total Lost Samples: (\d+)',text).group(1),'final_checksum':json.loads((d/'result.json').read_text())['finalChecksum']})
  if vals:
   med={k:S.median(v['counters'][k] for v in vals) for k in vals[0]['counters']}
   groups={k:S.median(v['groups'].get(k,0) for v in vals) for k in set().union(*(v['groups'] for v in vals))}
   out[sc][mode]={'runs':vals,'median_counters':med,'median_self_cycle_percent':groups}
(r/'profile-summary.json').write_text(json.dumps(out,indent=2))
for sc,ms in out.items():
 print(sc)
 for mode,a in ms.items():
  print(mode,len(a['runs']),{k:round(v,2) for k,v in a['median_self_cycle_percent'].items()})
  c=a['median_counters'];print('CPUms',c.get('task-clock'),'instructions B',round(c.get('instructions',0)/1e9,2),'cyclesB',round(c.get('cycles',0)/1e9,2),'cachemissM',round(c.get('cache-misses',0)/1e6,1),'context switches',c.get('context-switches'))
