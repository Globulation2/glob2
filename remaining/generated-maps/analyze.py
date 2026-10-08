from pathlib import Path
import json,random,math,statistics,sys
root=Path(sys.argv[1]) if len(sys.argv)>1 else Path('artifacts/resource-growth/remaining/generated-maps');raw=json.loads((root/'results.json').read_text());results=[]
for generator in sorted({r['generator'] for r in raw['samples']}):
 rows={(r['variant'],r['seed']):r for r in raw['samples'] if r['generator']==generator}
 seeds=sorted(s for v,s in rows if v=='immediate-reference')
 assert len(seeds)==20
 for material,index in [('wheat',1),('wood',0),('algae',4)]:
  old=[rows['immediate-reference',s] for s in seeds];new=[rows['owner',s] for s in seeds]
  for s,a,b in zip(seeds,old,new):
   assert a['initial']==b['initial']==rows['shared',s]['initial']
   assert b['final']==rows['shared',s]['final']
  a=[r['final'][0][index] for r in old];b=[r['final'][0][index] for r in new];initial=[r['initial'][0][index] for r in old]
  diffs=[y-x for x,y in zip(a,b)];rng=random.Random(713);boot=[];growth_boot=[]
  for _ in range(100000):
   ix=[rng.randrange(len(seeds)) for _ in seeds];den=sum(a[i] for i in ix)
   if den:boot.append(100*(sum(b[i] for i in ix)/den-1))
   growth_den=sum(a[i]-initial[i] for i in ix)
   if growth_den:growth_boot.append(100*(sum(b[i]-initial[i] for i in ix)/growth_den-1))
  boot.sort();growth_boot.sort();neg=sum(d<0 for d in diffs);pos=sum(d>0 for d in diffs);n=neg+pos
  p=min(1,2*sum(math.comb(n,k) for k in range(min(neg,pos)+1))/2**n) if n else 1
  avg=lambda x:statistics.mean(x)
  row={'generator':generator,'side':old[0]['side'],'material':material,'pairs':len(seeds),'initial_mean':avg(initial),'reference_final_mean':avg(a),'delayed_final_mean':avg(b),'difference_mean':avg(diffs),'difference_percent':100*(sum(b)/sum(a)-1) if sum(a) else None,'paired_bootstrap_95_percent':[boot[int(len(boot)*.025)-1],boot[int(len(boot)*.975)-1]] if boot else None,'growth_bootstrap_95_percent':[growth_boot[int(len(growth_boot)*.025)-1],growth_boot[int(len(growth_boot)*.975)-1]] if growth_boot else None,'reference_growth_mean':avg(a)-avg(initial),'delayed_growth_mean':avg(b)-avg(initial),'growth_difference_percent':100*((sum(b)-sum(initial))/(sum(a)-sum(initial))-1) if sum(a)!=sum(initial) else None,'terminal_flush_final_mean':avg([r['terminal_flush'][0][index] for r in new]),'reference_tiles_mean':avg([r['final'][1][index] for r in old]),'delayed_tiles_mean':avg([r['final'][1][index] for r in new]),'lower':neg,'higher':pos,'ties':len(seeds)-n,'sign_p':p}
  results.append(row)
prior=0
for rank,row in enumerate(sorted(results,key=lambda r:r['sign_p'])):
 prior=max(prior,min(1,row['sign_p']*(len(results)-rank)));row['holm_sign_p']=prior
report={'replicates':'20 independent generation seeds paired across variants per generator; owner/shared are not separate statistical replicates','method':'100000 paired seed-bootstrap draws; ratio of mean final stocks, percentile95% intervals (not multiplicity-adjusted). Two-sided exact paired sign tests with Holm correction over12 map/resource comparisons. Sign test concerns directional consistency, not mean effect.','scope':f"{raw['ticks']}tick no-harvesting ecology on default generated terrain; delay8. No timing inference or latest-master executable comparison. Terminal flush is diagnostic, not ordinary play.",'generation_failures':raw['generation_failures'],'results':results}
(root/'analysis.json').write_text(json.dumps(report,indent=2)+'\n')
lines=['| Generator | Resource | Immediate final | Delayed final | Difference [95% CI] | Growth-only change |','|---|---|---:|---:|---|---:|']
for r in results:
 ci=r['paired_bootstrap_95_percent'];pct=r['difference_percent'];growth=r['growth_difference_percent']
 lines.append(f"| {r['generator']} {r['side']}² | {r['material']} | {r['reference_final_mean']:.1f} | {r['delayed_final_mean']:.1f} | {pct:+.2f}% [{ci[0]:+.2f}, {ci[1]:+.2f}] | {growth:+.2f}% |" if pct is not None and growth is not None else f"| {r['generator']} | {r['material']} | {r['reference_final_mean']:.1f} | {r['delayed_final_mean']:.1f} | n/a | n/a |")
(root/'table.md').write_text('\n'.join(lines)+'\n');print('\n'.join(lines))
