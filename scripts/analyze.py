import json,statistics,random,math
from pathlib import Path
root=Path(__file__).parent/'results';d=json.loads((root/'manifest.json').read_text());pairs={}
for row in d['runs']:pairs.setdefault((row['family'],row['seed']),{})[row['variant']]=row
summary={};rng=random.Random(0)
def ci(values):
 if not values:return None
 samples=sorted(statistics.mean(rng.choices(values,k=len(values))) for _ in range(5000));return [samples[125],samples[4874]]
def totals(row):
 r=row['result'];return {'cpu_seconds':r['benchmark_run_cpu_ns']/1e9,'wall_seconds':row['wall_s'],'run_seconds':r['run_ns']/1e9,'rss_bytes':row['peak_rss_bytes'],'units':sum(t['units'] for t in r['teams']),'buildings':sum(t['buildings'] for t in r['teams']),'hungry_units':sum(t['statistics']['need_food'] for t in r['teams']),'alive_teams':sum(t['alive'] for t in r['teams']),**{k:sum(t['routing_comparison'][k] for t in r['teams']) for k in ['wheat_delivered','wheat_harvested','construction_completed','starvation_deaths']}}
for family in sorted({k[0] for k in pairs})+['all']:
 selected=[(k,v) for k,v in pairs.items() if (family=='all' or k[0]==family) and len(v)==2];table={}
 for metric in totals(selected[0][1]['round-trip']) if selected else []:
  base=[totals(v['round-trip'])[metric] for _,v in selected];after=[totals(v['greedy'])[metric] for _,v in selected];deltas=[a-b for a,b in zip(after,base)];relative=[100*(a/b-1) for a,b in zip(after,base) if b]
  ratio_boot=[]
  if sum(base):
   for _ in range(5000):
    indices=rng.choices(range(len(base)),k=len(base));den=sum(base[i] for i in indices)
    if den:ratio_boot.append(100*(sum(after[i] for i in indices)/den-1))
   ratio_boot.sort()
  if metric=='starvation_deaths':relative=[]
  table[metric]={'zero_baseline_matches':sum(b==0 for b in base),'pooled_percent_change_95ci':[ratio_boot[int(len(ratio_boot)*.025)],ratio_boot[min(len(ratio_boot)-1,int(len(ratio_boot)*.975))]] if ratio_boot else None,'round_trip_mean':statistics.mean(base),'greedy_mean':statistics.mean(after),'pooled_percent_change':100*(sum(after)/sum(base)-1) if sum(base) else None,'mean_paired_percent_change':statistics.mean(relative) if relative else None,'mean_paired_percent_change_95ci':ci(relative),'paired_absolute_change_95ci':ci(deltas),'individual_percent_changes':relative}
 summary[family]={'independent_seed_pairs':len(selected),'metrics':table}
(root/'summary.json').write_text(json.dumps(summary,indent=2));print(json.dumps(summary,indent=2))
