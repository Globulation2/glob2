import json,math,statistics
from pathlib import Path
p=Path(__file__).resolve().parent
rows=[json.loads(s) for s in (p/'profile-measurements.jsonl').read_text().splitlines()]
results=[]
for name in dict.fromkeys(r['case'] for r in rows):
 rs=[r for r in rows if r['case']==name];pairs=[]
 for i in range(4):
  pair={r['variant']:r for r in rs if r['repeat']==i}
  if 'before' in pair and 'after' in pair:pairs.append(pair)
 item={'case':name,'pairs':len(pairs)}
 if pairs:
  logs=[math.log(r['after']['cpu_s']/r['before']['cpu_s']) for r in pairs]
  mean=statistics.mean(logs)
  margin= {2:12.706,3:4.303,4:3.182}.get(len(logs),0)*statistics.stdev(logs)/math.sqrt(len(logs)) if len(logs)>1 else None
  item.update(cpu_saved_percent=100*(1-math.exp(mean)),ci95=None if margin is None else [100*(1-math.exp(mean+margin)),100*(1-math.exp(mean-margin))],pair_savings=[100*(1-math.exp(x)) for x in logs],before_cpu_s=statistics.mean(r['before']['cpu_s'] for r in pairs),after_cpu_s=statistics.mean(r['after']['cpu_s'] for r in pairs))
 if pairs:
  scopes=['gradient.building','gradient.building_resume']
  item['scope_ms']={v:{scope:statistics.mean(float(r[v]['performance'][scope]['total_ns'])/1e6 for r in pairs) for scope in scopes} for v in ['before','after']}
  item['scope_calls_match']=all(r['before']['performance'][scope]['calls']==r['after']['performance'][scope]['calls'] for r in pairs for scope in scopes)
 profiles={v:[r for r in rs if r['variant']==v+'-profile'] for v in ['before','after']}
 if all(profiles.values()):
  item['initialization']={}
  for kind in ['0','1']:
   means={v:statistics.mean(r['profile'][kind]['ns']/1e6 for r in rr) for v,rr in profiles.items()}
   item['initialization'][kind]={'before_ms':means['before'],'after_ms':means['after'],'saved_percent':100*(1-means['after']/means['before']) if means['before'] else None,'before_fill_ms':statistics.mean(r['profile'][kind]['fill_ns']/1e6 for r in profiles['before'])}
  item['counts_match']=all(all(r['profile'][kind][k]==profiles['before'][0]['profile'][kind][k] for k in ['cells','calls'] for kind in ['0','1']) for rr in profiles.values() for r in rr)
  a=sum(statistics.mean(r['profile'][kind]['ns'] for r in profiles['after']) for kind in ['0','1'])
  b=sum(statistics.mean(r['profile'][kind]['ns'] for r in profiles['before']) for kind in ['0','1'])
  item['all_initialization_saved_percent']=100*(1-a/b)
 results.append(item)
(p/'analysis.json').write_text(json.dumps(results,indent=2)+'\n')
print(json.dumps(results,indent=2))
