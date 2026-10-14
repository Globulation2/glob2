from pathlib import Path
import json,statistics,random
r=Path('/home/bradley/glob2-lean-candidate/artifacts/lean-compute/evidence');out=[]
for group in ['cpu-no-placeholders','cpu-ordinary-large-final']:
 rows=[json.loads(x) for x in (r/group/'measurements.jsonl').read_text().splitlines()]
 for scenario in sorted({x['scenario'] for x in rows}):
  for n in [1,2,4,8]:
   b={x['repeat']:x for x in rows if x['scenario']==scenario and x['repeat']>=0 and x['variant']==('baseline' if n==1 else f'baseline-{n}')};c={x['repeat']:x for x in rows if x['scenario']==scenario and x['repeat']>=0 and x['variant']==f'compute-{n}'};pairs=sorted(b.keys()&c.keys()); ratios=[c[i]['result']['benchmark_run_cpu_ns']/b[i]['result']['benchmark_run_cpu_ns'] for i in pairs];rng=random.Random(4242);boots=sorted(statistics.median(rng.choices(ratios,k=len(ratios))) for _ in range(10000))
   value=dict(workload=scenario,participants=n,pairs=len(pairs),group=group,warm_cpu_ratio=statistics.median(ratios),warm_cpu_ratio_bootstrap95=[boots[250],boots[9749]],metrics={key:dict(baseline=statistics.median(b[i][key] for i in pairs),candidate=statistics.median(c[i][key] for i in pairs),paired_ratio=statistics.median(c[i][key]/b[i][key] for i in pairs)) for key in ['cpu_s','wall_s','peak_rss_bytes']})
   for variant,data in [('baseline',b),('candidate',c)]:
    value[variant]={phase+'_cpu_s':statistics.median(data[i]['result']['benchmark_'+phase+'_cpu_ns']/1e9 for i in pairs) for phase in ['setup','run','save']};value[variant]['cpu_ns_per_tick']=statistics.median(data[i]['result']['benchmark_run_cpu_ns']/data[i]['result']['benchmark_measured_ticks'] for i in pairs)
   out.append(value);print(scenario,n,len(pairs),'warm CPU%',round((value['warm_cpu_ratio']-1)*100,2),'wholeCPU%',round((value['metrics']['cpu_s']['paired_ratio']-1)*100,2),'wall%',round((value['metrics']['wall_s']['paired_ratio']-1)*100,2),'RSS%',round((value['metrics']['peak_rss_bytes']['paired_ratio']-1)*100,2))
(r/'final-cpu-summary.json').write_text(json.dumps(out,indent=2)+'\n')
