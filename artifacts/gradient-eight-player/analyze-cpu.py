from pathlib import Path
import json,math,statistics,random
D=Path(__file__).parent
summary={}
for host in ['therig','devlaptop']:
 rows=sum((json.loads((D/f'{host}-{batch}.json').read_text()) for batch in ['quiet-results','quiet-extra-results']),[])
 assert len(rows)==512
 states={}
 for r in rows:
  key=(r['state'],r['tag']);assert r['ticks']>0
  states.setdefault(key,{})[r['variant']]=r
 assert len(states)==256
 ratios={};metadata={}
 for (state,tag),pair in states.items():
  a,b=pair['baseline'],pair['candidate'];assert a['ticks']==b['ticks'] and a['end']==b['end']
  ratios.setdefault(state,[]).append(math.log(b['cpu_seconds']/a['cpu_seconds']));metadata[state]=a['family']
 assert len(ratios)==64 and all(len(v)==4 for v in ratios.values())
 out={}
 for group in ['all','maxima','nicowar','baseline-origin','candidate-origin']:
  selected={s:v for s,v in ratios.items() if group=='all' or ('-origin' in group and s.endswith('-'+group.split('-')[0])) or ('-origin' not in group and '-'+group+'-' in s)}
  families={}
  for s,v in selected.items():families.setdefault(metadata[s],[]).append(s)
  rng=random.Random(106701);keys=sorted(families);draws=[]
  for _ in range(10000):
   draws.append(statistics.mean(statistics.mean(statistics.mean(rng.choices(selected[s],k=4)) for s in families[g]) for g in rng.choices(keys,k=len(keys))))
  draws.sort();estimate=statistics.mean(statistics.mean(statistics.mean(selected[s]) for s in ss) for ss in families.values())
  out[group]={'cpu_change_percent':100*math.expm1(estimate),'ci95':[100*math.expm1(draws[i]) for i in [250,9750]],'states':len(selected),'families':len(keys)}
 summary[host]=out
(D/'cpu-summary.json').write_text(json.dumps(summary,indent=2));print(json.dumps(summary,indent=2))
