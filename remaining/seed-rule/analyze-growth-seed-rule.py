from pathlib import Path
import json,statistics,random,math
root=Path('artifacts/resource-growth/remaining/seed-rule')
runs={v:json.loads((root/f'{v}.json').read_text()) for v in ['baseline','candidate']}
lookups={v:{(r['layout'],r['rate'],r['seed'],r['shared']):r for r in run['samples']} for v,run in runs.items()}
rng=random.Random(714);ixs=[[rng.randrange(20) for _ in range(20)] for _ in range(30000)]
def contrast(a,b):
 dif=[x-y for x,y in zip(a,b)];boot=sorted(100*(sum(a[i] for i in ix)/sum(b[i] for i in ix)-1) for ix in ixs if sum(b[i] for i in ix))
 npos=sum(x>0 for x in dif);nneg=sum(x<0 for x in dif);n=npos+nneg
 p=min(1,2*sum(math.comb(n,k) for k in range(min(npos,nneg)+1))/2**n) if n else 1
 return {'percent':100*(sum(a)/sum(b)-1),'ci95_percent':[boot[int(.025*len(boot))],boot[int(.975*len(boot))]],'absolute_units':statistics.mean(dif),'sign_p':p,'positive_pairs':npos,'negative_pairs':nneg,'ties':20-n}
rows=[]
for layout in ['grass-control','river','islands','dry','mixed']:
 for rate in [196608,49152,0]:
  data={v:[lookups[v][layout,rate,s,True] for s in range(1,21)] for v in runs}
  for v in runs:
   a=lookups[v][layout,rate,1,True];b=lookups[v][layout,rate,1,False];assert a['trace']==b['trace']
  for a,b in zip(data['baseline'],data['candidate']):assert a['initial']==b['initial']
  if rate==196608:
   assert all(a['trace']==b['trace'] for a,b in zip(data['baseline'],data['candidate']))
  row={'layout':layout,'paper_rate_fraction':rate/196608,'width':data['baseline'][0]['width'],'height':data['baseline'][0]['height']}
  growth={}
  for v,samples in data.items():
   gain=[[r['trace'][-1]['stocks'][i]-r['initial'][i] for i in range(3)] for r in samples]
   growth[v]=[g[0]+g[1] for g in gain]
   row[v]={'initial_stock':statistics.mean(sum(r['initial'][:2]) for r in samples),'initial_deposits':statistics.mean(r['initial'][2] for r in samples),'food_added':statistics.mean(g[0] for g in gain),'paper_added':statistics.mean(g[1] for g in gain),'deposits_added':statistics.mean(g[2] for g in gain),'total_added':statistics.mean(growth[v]),'stock_units_per_tick':statistics.mean(growth[v])/512,'proposals':statistics.mean(r['proposals'] for r in samples),'sampled':statistics.mean(r['sampled'] for r in samples),'checkpoint_total_stock':{str(t):statistics.mean(sum(next(c['stocks'][:2] for c in r['trace'] if c['tick']==t)) for r in samples) for t in range(64,513,64)}}
  row['growth_difference']=contrast(growth['candidate'],growth['baseline'])
  row['spread_difference']=contrast([r['trace'][-1]['stocks'][2]-r['initial'][2] for r in data['candidate']],[r['trace'][-1]['stocks'][2]-r['initial'][2] for r in data['baseline']])
  rows.append(row)
# Primary family: total-stock accumulation, all 15 layout/rate combinations.
prior=0
for rank,r in enumerate(sorted(rows,key=lambda r:r['growth_difference']['sign_p'])):
 prior=max(prior,min(1,r['growth_difference']['sign_p']*(15-rank)));r['growth_difference']['holm_p']=prior
result={'method':'20 matched seeds per cell. 30000 paired percentile bootstrap samples (RNG714), ratio of mean NEW stock, not final stocks. Pointwise95% CIs. Two-sided exact sign tests with Holm across15 primary growth contrasts. Spread comparisons exploratory. Dry-map percentage estimates unstable because few events. Owner repeats are verification, not extra samples.','results':rows}
(root/'analysis.json').write_text(json.dumps(result,indent=2)+'\n')
for r in rows:
 print(r['layout'],r['paper_rate_fraction'],r['growth_difference'],'spread',r['spread_difference']['percent'])
