from pathlib import Path
import sys,json,statistics
sys.path.insert(0,'test');from benchmark_resource_growth import interval
base=Path('artifacts/resource-growth/remaining');mode=sys.argv[1] if len(sys.argv)>1 else 'timing';root=base/mode
rows=[json.loads(l) for l in (root/'measurements.jsonl').read_text().splitlines()]
summary={}
def measured(values):return {'median':statistics.median(values),'ci95':interval(values)}
for candidate in sorted({r['candidate'] for r in rows}):
 result=summary[candidate]={}
 for scenario in sorted({r['scenario'] for r in rows}):
  pairs={}
  for r in rows:
   if r['candidate']==candidate and r['scenario']==scenario and r['repeat']>=0:pairs.setdefault(r['repeat'],{})[r['variant']]=r
  pairs=[p for p in pairs.values() if len(p)==2]
  if len(pairs)<10:continue
  result[scenario]={'pairs':len(pairs),
    'baseline_ms':statistics.median(p['baseline']['result']['run_ns']/1e6 for p in pairs),
    'candidate_ms':statistics.median(p[candidate]['result']['run_ns']/1e6 for p in pairs),
    'baseline_cpu_ms':statistics.median(p['baseline']['result']['benchmark_run_cpu_ns']/1e6 for p in pairs),
    'candidate_cpu_ms':statistics.median(p[candidate]['result']['benchmark_run_cpu_ns']/1e6 for p in pairs),
    'baseline_peak_rss_bytes':statistics.median(p['baseline']['peak_rss_bytes'] for p in pairs),
    'candidate_peak_rss_bytes':statistics.median(p[candidate]['peak_rss_bytes'] for p in pairs),
    'tps_gain':measured([p['baseline']['result']['run_ns']/p[candidate]['result']['run_ns']-1 for p in pairs]),
    'cpu_reduction':measured([1-p[candidate]['result']['benchmark_run_cpu_ns']/p['baseline']['result']['benchmark_run_cpu_ns'] for p in pairs]),
    'saved_ms':measured([(p['baseline']['result']['run_ns']-p[candidate]['result']['run_ns'])/1e6 for p in pairs]),
    'cpu_saved_ms':measured([(p['baseline']['result']['benchmark_run_cpu_ns']-p[candidate]['result']['benchmark_run_cpu_ns'])/1e6 for p in pairs]),
    'p99_change':(measured([p[candidate]['result']['tick_p99_ns']/p['baseline']['result']['tick_p99_ns']-1 for p in pairs]) if all('tick_p99_ns' in p[candidate]['result'] for p in pairs) else {'available':False,'reason':'untouched master exposes only the logarithmic tick histogram'}),
    'rss_change':measured([p[candidate]['peak_rss_bytes']/p['baseline']['peak_rss_bytes']-1 for p in pairs])}
primary=['dense','multi','ai512','disabled512'];decisions={}
for c,results in summary.items():
 if c in ('control','master'):continue
 wins=[(name,k) for name,d in results.items() for k in ('tps_gain','cpu_reduction') if d[k]['median']>=.01 and d[k]['ci95'][0]>0]
 safe=all(p in results and all(results[p][k]['ci95'][0]>-.02 for k in ('tps_gain','cpu_reduction')) for p in primary)
 regressions=[(name,k) for name,d in results.items() for k in ('tps_gain','cpu_reduction') if d[k]['ci95'][1]<-.02]
 investigations=[(name,k) for name,d in results.items() for k in ('p99_change','rss_change') if d[k]['median']>.05]
 capped=[(name,k) for name,d in results.items() if name in primary and d['pairs']>=30 for k in ('tps_gain','cpu_reduction') if d[k]['ci95'][0]<=-.02]
 decisions[c]={'wins':wins,'rules_out_primary_regressions':safe,'demonstrated_regressions':regressions,'investigate':investigations,'qualifies_numerically':bool(wins) and safe and not regressions,'unresolved_at_30_pair_limit':capped,'requires_extension':not (bool(wins) and safe) and not capped and not regressions and any(d['pairs']<30 for d in results.values())}
(root/'summary.json').write_text(json.dumps({'results':summary,'decisions':decisions},indent=2))
for c,results in summary.items():
 print(c)
 for sc,d in results.items():print(' ',sc,d['pairs'],'TPS',*[round(v*100,2) for v in [d['tps_gain']['median'],*d['tps_gain']['ci95']]],'CPU',*[round(v*100,2) for v in [d['cpu_reduction']['median'],*d['cpu_reduction']['ci95']]])
print(json.dumps(decisions,indent=2))
