from pathlib import Path
import json,random,math,statistics
root=Path('artifacts/resource-growth/remaining/configured-seeds')
raw=json.loads((root/'ecology.json').read_text())['samples']
results=[]
for scenario in ['dense','sparse','multi','saturated','blocked','disabled']:
 by={(r['variant'],r['seed']):r for r in raw if r['scenario']==scenario}
 old=[by['immediate-reference',s] for s in range(1,21)]
 new=[by['owner',s] for s in range(1,21)]
 assert all(a['initial']==b['initial'] for a,b in zip(old,new))
 a=[sum(r['final'][:-1]) for r in old];b=[sum(r['final'][:-1]) for r in new]
 differences=[y-x for x,y in zip(a,b)]
 rng=random.Random(713);boot=[]
 for _ in range(100000):
  ix=[rng.randrange(20) for _ in range(20)]
  boot.append(100*(sum(b[i] for i in ix)/sum(a[i] for i in ix)-1))
 boot.sort()
 neg=sum(d<0 for d in differences);pos=sum(d>0 for d in differences);n=neg+pos
 p=min(1,2*sum(math.comb(n,k) for k in range(min(neg,pos)+1))/2**n) if n else 1
 row={'scenario':scenario,'n':20,'difference_percent':100*(sum(b)/sum(a)-1),'paired_bootstrap_95_ci_percent':[boot[2499],boot[97499]],'mean_absolute_difference':statistics.mean(differences),'lower_pairs':neg,'higher_pairs':pos,'ties':20-n,'two_sided_exact_sign_p':p,'old_mean_deposits':statistics.mean(r['final'][-1] for r in old),'new_mean_deposits':statistics.mean(r['final'][-1] for r in new),'old_growth_units':statistics.mean(sum(r['final'][:-1])-sum(r['initial'][:-1]) for r in old),'new_growth_units':statistics.mean(sum(r['final'][:-1])-sum(r['initial'][:-1]) for r in new)}
 results.append(row)
prior=0
for rank,r in enumerate(sorted(results,key=lambda r:r['two_sided_exact_sign_p'])):
 prior=max(prior,min(1,r['two_sided_exact_sign_p']*(6-rank)));r['holm_adjusted_sign_p']=prior
report={'replicates': '20 paired seeds, not individual ticks or owner/shared copies','bootstrap':'100000 seed-pair resamples; ratio of mean final stocks; percentile 95% CI, not multiplicity-adjusted','test':'Two-sided exact paired sign test, Holm adjustment across six scenarios; tests direction consistency, not a parametric mean difference','scope':'Fixed scenarios and512tick horizon, retained immediate reference vs delayed full game steps; no causal attribution from this comparison','results':results}
(root/'player-free-significance.json').write_text(json.dumps(report,indent=2)+'\n')
for r in results:print(r)
