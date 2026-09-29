from pathlib import Path
import json,math,random,statistics
D=Path(__file__).parent
rows=sum((json.loads((D/f'extracted-{host}.json').read_text()) for host in ['therig','devlaptop']),[])
assert len(rows)==256 and len({r['case']['id'] for r in rows})==256
summary={'failed':[r for r in rows if r['status']=='failed'],'groups':{}}
for ai in ['maxima','nicowar']:
 rr=[r for r in rows if r['status']=='complete' and r['case']['players'][0]==ai]
 out={'completed_pairs':len(rr),'metrics':{}}
 for window in ['common','early']:
  for metric,denom,logarithmic in [('wheat',None,True),('meals',None,True),('unit_ticks',None,True),('starved','unit_ticks',False),('critical_ticks','unit_ticks',False)]:
   fs={};excluded=[]
   for r in rr:
    if window not in r['variants']['baseline'] or window not in r['variants']['candidate']:excluded.append(r['case']['id']);continue
    vals={}
    for v in ['baseline','candidate']:
     m=r['variants'][v][window];den=m[denom] if denom else (r['end'] if window=='common' else 16384)
     vals[v]=m[metric]/den if den else None
    if any(x is None for x in vals.values()) or (logarithmic and min(vals.values())<=0):excluded.append(r['case']['id']);continue
    value=math.log(vals['candidate']/vals['baseline']) if logarithmic else vals['candidate']-vals['baseline']
    fs.setdefault(r['case']['generator'],[]).append(value)
   keys=sorted(fs);rng=random.Random(106501);draws=[]
   for _ in range(10000):
    families=rng.choices(keys,k=len(keys));draws.append(statistics.mean(statistics.mean(rng.choices(fs[g],k=len(fs[g]))) for g in families))
   draws.sort();scale=(lambda x:100*math.expm1(x)) if logarithmic else (lambda x:x*(1e6 if metric=='starved' else 100))
   out['metrics'][window+'_'+metric]={'estimate':scale(statistics.mean(statistics.mean(v) for v in fs.values())),'ci95':[scale(draws[250]),scale(draws[9750])],'units':'percent change' if logarithmic else 'per million unit-ticks' if metric=='starved' else 'percentage points','families':len(fs),'excluded_pairs':excluded}
 summary['groups'][ai]=out
(D/'gameplay-summary.json').write_text(json.dumps(summary,indent=2))
print(json.dumps(summary['groups'],indent=2))
