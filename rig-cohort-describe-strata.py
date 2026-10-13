from pathlib import Path
import json,collections,random,hashlib
p=Path(__file__).resolve().parent;rows=json.load(open(p/'match-summary.json'));groups=collections.defaultdict(lambda:collections.defaultdict(list))
for r in rows:groups[r['stratum']][r['arm']].append(r)
metrics={'starvation_rate':('starvation_deaths','unit_ticks'),'critical_hunger_fraction':('critical_hunger_ticks','unit_ticks'),'construction_rate':('new_buildings','worker_ticks')};result={};rng=random.Random(713051)
for s in sorted(groups):
 vectors={a:[[r[k] for k in ['starvation_deaths','unit_ticks','critical_hunger_ticks','new_buildings','worker_ticks']] for r in groups[s][a]] for a in ['original','cached-material']}
 samples={m:[] for m in metrics}
 for draw in range(10000):
  totals=[]
  for a in ['original','cached-material']:
   v=vectors[a];t=[0]*5
   for _ in v:
    row=v[rng.randrange(len(v))]
    for k in range(5):t[k]+=row[k]
   totals.append(t)
  x,y=totals;ra=[x[0]/x[1],x[2]/x[1],x[3]/x[4]];rb=[y[0]/y[1],y[2]/y[1],y[3]/y[4]]
  for i,m in enumerate(metrics):samples[m].append(rb[i]/ra[i] if ra[i] else float('inf'))
 out={}
 for m,(num,den) in metrics.items():
  a,b=groups[s]['original'],groups[s]['cached-material'];an=sum(r[num] for r in a);ad=sum(r[den] for r in a);bn=sum(r[num] for r in b);bd=sum(r[den] for r in b);v=sorted(samples[m]);out[m]={'original_events':an,'original_exposure':ad,'cached_events':bn,'cached_exposure':bd,'ratio':(bn/bd)/(an/ad),'unadjusted_descriptive_95_ratio_interval':[v[249],v[9749]]}
 result[s]=out
report={'scope':'Exploratory descriptive subgroup intervals added after the primary results. These are individual, unadjusted95% match-bootstrap intervals, not preregistered subgroup discovery tests or simultaneous coverage across12strata×3endpoints. Primary analysis and its multiplicity correction remain unchanged. Completed-match selection applies; river/nicowar has49cached completions.','draws_per_stratum':10000,'seed':713051,'input_sha256':hashlib.sha256((p/'match-summary.json').read_bytes()).hexdigest(),'strata':result}
(p/'stratum-intervals.json').write_text(json.dumps(report,indent=2));print(json.dumps(result['river/four-ai'],indent=2))
