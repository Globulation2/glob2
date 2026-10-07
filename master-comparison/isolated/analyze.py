import csv,json,statistics,pathlib,math
root=pathlib.Path(__file__).resolve().parent
rows=[json.loads(l) for l in (root/'results.jsonl').read_text().splitlines()]
measure=[r for r in rows if r['phase'] in ('measure','control')]
keys=['tps','cpu_us_per_tick','owner_cpu_us_per_tick','process_cpu_percent','rss_median_bytes','rss_max_bytes','process_peak_rss_bytes','simulation_ns_p50','simulation_ns_p95','simulation_ns_p99','owner_iteration_ns_p50','owner_iteration_ns_p95','owner_iteration_ns_p99','completion_interval_ns_p50','completion_interval_ns_p95','completion_interval_ns_p99','frame_p50_ns','frame_p95_ns','scene_age_p95_ms']
summaries=[]
for phase,case,cores in sorted(set((r['phase'],r['case'],r['cores']) for r in measure)):
 selected=[r for r in measure if (r['phase'],r['case'],r['cores'])==(phase,case,cores)]
 variants={v:[r for r in selected if r['variant']==v] for v in ('baseline','final')}
 assert len(variants['baseline'])==len(variants['final'])
 summary={'phase':phase,'case':case,'cores':cores,'pairs':len(variants['baseline'])}
 for variant,samples in variants.items():
  summary[variant]={k:{'median':statistics.median(r[k] for r in samples),'min':min(r[k] for r in samples),'max':max(r[k] for r in samples)} for k in keys}
 summary['paired_changes_percent']={}
 for key in keys:
  ratios=[]
  for a in variants['baseline']:
   b=next(r for r in variants['final'] if r['repeat']==a['repeat'])
   if a[key]:ratios.append(100*(b[key]/a[key]-1))
  if ratios:summary['paired_changes_percent'][key]={'median':statistics.median(ratios),'min':min(ratios),'max':max(ratios)}
 summaries.append(summary)
(root/'summary.json').write_text(json.dumps(summaries,indent=2)+'\n')
with (root/'all-results.csv').open('w') as f:
 w=csv.DictWriter(f,fieldnames=list(rows[0]));w.writeheader();w.writerows(rows)
for r in summaries:
 def med(v,k,scale=1):return r[v][k]['median']/scale
 print(r['phase'],r['case'],r['cores'],r['pairs'],'pairs')
 for k,scale in [('tps',1),('cpu_us_per_tick',1000),('rss_median_bytes',2**20),('owner_iteration_ns_p95',1e6),('completion_interval_ns_p95',1e6)]:
  print(' ',k,round(med('baseline',k,scale),3),'->',round(med('final',k,scale),3),'paired%',r['paired_changes_percent'].get(k))
