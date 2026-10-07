"""Equal-family paired summaries; independent matches are the resampling unit."""
import argparse,gzip,json,random,statistics
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('evidence',type=Path,nargs='+');p.add_argument('--output',type=Path,required=True);a=p.parse_args()
rows=[]; starts={}
for evidence in a.evidence:
 manifest=json.loads((evidence/'timing-manifest.json').read_text())
 for scenario in json.loads((evidence/'preparation.json').read_text())['runs']:
  starts[(evidence.name.removeprefix('stress-'),scenario['id'])]=scenario['warmup']['result']
 for entry in manifest['runs']:
  with gzip.open(evidence/entry['measurement'],'rt') as f: row=json.load(f)
  row['host']=evidence.name.removeprefix('stress-');rows.append(row)
blocks={}
for row in rows:
 key=(row['host'],row['scenario'],row['repeat'],row['delay'],row['workers'])
 blocks.setdefault(key,{})[row['role']]=row
def counts(result):
 return {metric:sum(t.get('routing_comparison',{}).get(metric,0) for t in result['teams']) for metric in ['wheat_delivered','wheat_harvested','starvation_deaths','construction_completed']}
def tail(result,q=.95):
 histogram=result['benchmark_tick_histogram'];threshold=result['benchmark_measured_ticks']*q;seen=0;lower=0
 for bucket in histogram:
  seen+=bucket['count'];upper=bucket['upper_ns_exclusive']
  if seen>=threshold:return [lower,upper]
  lower=upper
 return [None,None]
paired=[]
for key,block in blocks.items():
 if not all(role in block for role in ['original-before','before','after','original-after']):continue
 original=[block[r]['run']['result'] for r in ['original-before','original-after']]
 current=[block[r]['run']['result'] for r in ['before','after']]
 wall=statistics.mean(r['run_ns'] for r in original);cpu=statistics.mean(r['benchmark_run_cpu_ns'] for r in original)
 newwall=statistics.mean(r['run_ns'] for r in current);newcpu=statistics.mean(r['benchmark_run_cpu_ns'] for r in current)
 totalcpu=statistics.mean(block[r]['run']['cpu_s'] for r in ['original-before','original-after'])
 for role,row in block.items():
  if row['quantile'] is None:continue
  result=row['run']['result']
  initial=counts(starts[(row['host'],row['scenario'])]);old=counts(original[0]);new=counts(result)
  outcome_delta={metric:new[metric]-old[metric] for metric in old}
  outcome_baseline={metric:old[metric]-initial[metric] for metric in old}
  if any(value<0 for value in outcome_baseline.values()):raise RuntimeError('Cumulative outcome counter reset; inspect loading before attribution')
  paired.append(dict(outcome_delta=outcome_delta,outcome_baseline=outcome_baseline,p95_tick_ns_bounds=tail(result),original_p95_tick_ns_bounds=tail(original[0]),host=row['host'],scenario=row['scenario'],family=row['family'],repeat=row['repeat'],delay=row['delay'],workers=row['workers'],quantile=row['quantile'],speed_ratio=wall/result['run_ns'],cpu_ratio=result['benchmark_run_cpu_ns']/cpu,total_process_cpu_ratio=row['run']['cpu_s']/totalcpu,new_baseline_speed_ratio=newwall/result['run_ns'],new_baseline_cpu_ratio=result['benchmark_run_cpu_ns']/newcpu,scheduler_off_speed_ratio=wall/newwall,scheduler_off_cpu_ratio=newcpu/cpu,tps=result['benchmark_measured_ticks']/(result['run_ns']/1e9),cpu_ms_per_tick=result['benchmark_run_cpu_ns']/result['benchmark_measured_ticks']/1e6,rss_bytes=row['run']['peak_rss_bytes']))
keys=['speed_ratio','cpu_ratio','total_process_cpu_ratio','new_baseline_speed_ratio','new_baseline_cpu_ratio','scheduler_off_speed_ratio','scheduler_off_cpu_ratio','tps','cpu_ms_per_tick','rss_bytes']
configs={}
for row in paired:configs.setdefault((row['quantile'],row['delay'],row['workers']),[]).append(row)
def macro(matches,key):
 families={}
 for row in matches:families.setdefault(row['family'],[]).append(row[key])
 return statistics.mean(statistics.mean(values) for values in families.values())
summary=[]
for config,selected in sorted(configs.items()):
 matches={}
 for row in selected:matches.setdefault((row['host'],row['scenario']),[]).append(row)
 averages=[dict(host=k[0],scenario=k[1],family=v[0]['family'],repetitions=len(v),outcome_delta={m:statistics.mean(r['outcome_delta'][m] for r in v) for m in v[0]['outcome_delta']},outcome_baseline=v[0]['outcome_baseline'],p95_tick_ns_bounds=v[0]['p95_tick_ns_bounds'],original_p95_tick_ns_bounds=v[0]['original_p95_tick_ns_bounds'],**{metric:statistics.mean(r[metric] for r in v) for metric in keys}) for k,v in matches.items()]
 strata={}
 for row in averages:strata.setdefault((row['family'],row['host']),[]).append(row)
 rng=random.Random(39137);bootstrap={key:[] for key in ['speed_ratio','cpu_ratio']}
 for _ in range(10000):
  sample=[rng.choice(values) for values in strata.values() for _ in values]
  for metric in bootstrap:bootstrap[metric].append(macro(sample,metric))
 intervals={}
 for metric,values in bootstrap.items():
  values.sort();intervals[metric]=[values[249],values[9749]]
 summary.append(dict(quantile=config[0],delay=config[1],workers=config[2],matches=len(averages),families=len({r['family'] for r in averages}),metrics={metric:macro(averages,metric) for metric in keys if metric not in ['tps','cpu_ms_per_tick','rss_bytes']},ci95=intervals,by_family={family:{metric:statistics.mean(r[metric] for r in averages if r['family']==family) for metric in keys if metric not in ['tps','cpu_ms_per_tick','rss_bytes']} for family in sorted({r['family'] for r in averages})},by_host={host:{metric:macro([r for r in averages if r['host']==host],metric) for metric in keys} for host in sorted({r['host'] for r in averages})},match_results=averages))
a.output.write_text(json.dumps(dict(raw_runs=len(rows),complete_blocks=len({(r['host'],r['scenario'],r['repeat'],r['delay'],r['workers']) for r in paired}),resampling='10000 bootstrap draws of whole matches, stratified by family and host; repeated runs averaged before resampling',points=summary),indent=2)+'\n')
print('Runs',len(rows),'points',len(summary),'retained paired measurements',len(paired))
