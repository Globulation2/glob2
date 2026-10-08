from pathlib import Path
import json,random,statistics,math
root=Path('artifacts/resource-growth/remaining/delay-ablation');delays=[1,2,3,4,8,12,16]
runs={d:json.loads((root/f'delay-{d}.json').read_text()) for d in delays}
lookup={d:{(s['scenario'],s['seed'],s['variant']):s for s in r['samples']} for d,r in runs.items()}
scenarios=['dense','sparse','multi','saturated','blocked','disabled']
previous=json.loads((root.parent/'player-free-results.json').read_text())
for row in previous['samples']:
 current=lookup[8][row['scenario'],row['seed'],row['variant']]
 for key in ['initial','final','statistics']:assert row[key]==current[key],('previous delay8',key)

for d in delays:
 for sc in scenarios:
  for seed in range(1,21):
   a=lookup[d][sc,seed,'owner']; b=lookup[d][sc,seed,'shared'];old=lookup[d][sc,seed,'immediate-reference']
   assert a['initial']==b['initial']==old['initial']==lookup[8][sc,seed,'owner']['initial']
   for key in ['final','statistics','checkpoints','pipeline','terminal_flush']:assert a[key]==b[key],(d,sc,seed,key)
   assert old==lookup[8][sc,seed,'immediate-reference']
rng=random.Random(713);resamples=[[rng.randrange(20) for _ in range(20)] for _ in range(30000)]
def compare(a,b):
 dif=[x-y for x,y in zip(a,b)];boot=sorted(100*(sum(a[i] for i in ix)/sum(b[i] for i in ix)-1) for ix in resamples)
 lo=sum(x<0 for x in dif);hi=sum(x>0 for x in dif);n=lo+hi
 p=min(1,2*sum(math.comb(n,k) for k in range(min(lo,hi)+1))/2**n) if n else 1
 return {'percent':100*(sum(a)/sum(b)-1),'ci95_percent':[boot[749],boot[29249]],'absolute':statistics.mean(dif),'sign_p':p,'lower_pairs':lo,'higher_pairs':hi,'ties':20-n}
results=[]
for sc in scenarios:
 baseline=[lookup[8][sc,s,'owner'] for s in range(1,21)]
 old=[lookup[8][sc,s,'immediate-reference'] for s in range(1,21)]
 for d in delays:
  rows=[lookup[d][sc,s,'owner'] for s in range(1,21)]
  stocks=[sum(r['final'][:-1]) for r in rows];base=[sum(r['final'][:-1]) for r in baseline];orig=[sum(r['final'][:-1]) for r in old]
  flushed=[sum(r['terminal_flush'][:-1]) for r in rows];baseflush=[sum(r['terminal_flush'][:-1]) for r in baseline]
  r={'scenario':sc,'delay':d,'mean_stock':statistics.mean(stocks),'mean_flushed_stock':statistics.mean(flushed),'mean_deposits':statistics.mean(r['final'][-1] for r in rows),'vs_delay8':compare(stocks,base),'vs_immediate':compare(stocks,orig),'flushed_vs_delay8':compare(flushed,baseflush),'flushed_vs_immediate':compare(flushed,orig),'pipeline_means':{k:statistics.mean(r['pipeline'][k] for r in rows) for k in rows[0]['pipeline']},'checkpoint_mean_stocks':{str(t):statistics.mean(sum(next(c['stocks'] for c in r['checkpoints'] if c['tick']==t)[:-1]) for r in rows) for t in [128,256,384,512]}}
  results.append(r)
# Primary family: six non-baseline delays x six scenarios. Exploratory terminal
# flush/reference contrasts have pointwise intervals, no confirmatory p-values.
prior=0
family=sorted([r for r in results if r['delay']!=8],key=lambda r:r['vs_delay8']['sign_p'])
for rank,r in enumerate(family):
 prior=max(prior,min(1,r['vs_delay8']['sign_p']*(len(family)-rank)))
 r['vs_delay8']['holm_p']=prior
report={'method':'20 matched seed pairs;30000 percentile bootstrap resamples, RNG713; percentage=ratio of mean final stocks minus1. Intervals are pointwise. Primary exact two-sided sign tests use Holm across36 delay-vs8 contrasts, testing directional consistency. Flush/reference contrasts exploratory. Owner/shared are not independent replicates.','results':results}
(root/'analysis.json').write_text(json.dumps(report,indent=2)+'\n')
lines=['# Player-free delay ablation','',report['method'],'','Only publication delay changes in the new pipeline.20seeds,512ticks,64² maps,six scenarios,no player seats/units/buildings/harvesting. Each delay has360runs including repeated immediate reference controls;2520runs total. Owner/shared exact per-tick checksums/statistics pass; report stocks,checkpoints and terminal flush also match. Starting inputs and immediate reference outputs match across all delays. No performance claim.','','## Final stock relative to immediate reference (%)','','| Delay | Dense | Sparse | Multi | Saturated | Blocked | Disabled |','|---|---:|---:|---:|---:|---:|---:|']
by={(r['scenario'],r['delay']):r for r in results}
for d in delays:lines.append('| '+str(d)+' | '+' | '.join(f"{by[sc,d]['vs_immediate']['percent']:+.2f}%" for sc in scenarios)+' |')
for contrast in ['vs_delay8','flushed_vs_delay8']:
 lines+=['',f'## {contrast}: paired percentage differences [pointwise95% CI]','','| Delay | Dense | Sparse | Multi | Saturated | Blocked | Disabled |','|---|---:|---:|---:|---:|---:|---:|']
 for d in delays:
  cells=[]
  for sc in scenarios:
   a=by[sc,d][contrast];cells.append(f"{a['percent']:+.2f} [{a['ci95_percent'][0]:+.2f},{a['ci95_percent'][1]:+.2f}]")
  lines.append('| '+str(d)+' | '+' | '.join(cells)+' |')
lines+=['','The terminal flush applies existing proposals in order without calculating further batches; it is diagnostic, not an additional normal simulation period. Normal endpoints do not publish early. It removes the final queue tail but cannot undo earlier delays in seeding, reproduction or snapshot feedback.','', 'This is the retained immediate reference, not the latest master executable. Eight-tick historical results must reproduce exactly. Fixed fixtures and20seeds do not establish effects on arbitrary maps or long-run equilibrium. Linuxx86-64 only; non-Linux/threadless unavailable. Heavy checksums join workers each tick, so this is not throughput or overlapping-compute evidence.','', 'Reproduction: build the release engine test binary using build.log, then run run-growth-delay-ablation.py and analyze-growth-delay-ablation.py from the repository root. The manifest and source patch identify inputs/binary/source.']
(root/'README.md').write_text('\n'.join(lines)+'\n')
print('\n'.join(lines[:17]))
for sc in ['dense','multi']:
 for d in [1,16]:print(sc,d,by[sc,d]['vs_delay8'], 'flush',by[sc,d]['flushed_vs_delay8'])
