#!/usr/bin/env python3
import collections
import json
import math
from pathlib import Path
import statistics

OUT=Path(__file__).resolve().parent
plan=json.loads((OUT/'plan.json').read_text())
rows=[json.loads(x) for x in (OUT/'measurements.jsonl').read_text().splitlines()]
T={1:12.706,2:4.303,3:3.182,4:2.776,5:2.571,6:2.447,7:2.365,8:2.306,9:2.262,10:2.228,11:2.201,12:2.179,13:2.160,14:2.145,15:2.131,16:2.120,17:2.110,18:2.101,19:2.093,20:2.086,21:2.080,22:2.074,23:2.069,24:2.064}

def paired(blocks,reference,change,metric=lambda r:r['cpu_s']):
    logs=[math.log(metric(b[change])/metric(b[reference])) for b in blocks if metric(b[reference])>0 and metric(b[change])>0]
    n=len(logs)
    if not n:return None
    mean=statistics.mean(logs);se=statistics.stdev(logs)/math.sqrt(n) if n>1 else None
    margin=T.get(n-1,1.96)*se if se is not None else None
    return {'n':n,'saving_percent':100*(1-math.exp(mean)),
            'ci95_percent':None if margin is None else [100*(1-math.exp(mean+margin)),100*(1-math.exp(mean-margin))],
            'block_savings_percent':[100*(1-math.exp(x)) for x in logs],
            'median_block_saving_percent':100*(1-math.exp(statistics.median(logs)))}

def difference(blocks,reference,change,metric):
    values=[metric(b[change])-metric(b[reference]) for b in blocks]
    n=len(values);mean=statistics.mean(values)
    margin=T.get(n-1,1.96)*statistics.stdev(values)/math.sqrt(n) if n>1 else None
    return {'n':n,'mean_difference':mean,'ci95':None if margin is None else [mean-margin,mean+margin], 'block_differences':values}

def scope(r,name,key='total_ns'):
    return float(r['performance'].get(name,{}).get(key,0))

def stable(b):
    power={r[edge]['power_source'] for r in b.values() for edge in ['before','after']}
    return len(power)==1

summary=[]
for c in plan['cases']:
    grouped=collections.defaultdict(dict)
    for r in rows:
        if r['case']==c['name']:grouped[r['repeat']][r['variant']]=r
    blocks=[b for _,b in sorted(grouped.items()) if set(b)=={'base','eager','lazy'}]
    if not blocks:continue
    result={'case':c['name'],'family':c['family'],'blocks':len(blocks),'planned_blocks':c['repetitions'],
            'refactor_vs_base':paired(blocks,'base','eager'),'lazy_vs_eager':paired(blocks,'eager','lazy'),
            'lazy_vs_base':paired(blocks,'base','lazy'),
            'stable_power_lazy_vs_eager':paired([b for b in blocks if stable(b)],'eager','lazy'),
            'wall_lazy_vs_eager':paired(blocks,'eager','lazy',lambda r:r['wall_s']),
            'variants':{}}
    for v in ['base','eager','lazy']:
        a=[b[v] for b in blocks]
        result['variants'][v]={'cpu_mean_s':statistics.mean(r['cpu_s'] for r in a),
          'cpu_median_s':statistics.median(r['cpu_s'] for r in a),
          'cpu_cv_percent':100*statistics.stdev(r['cpu_s'] for r in a)/statistics.mean(r['cpu_s'] for r in a) if len(a)>1 else None,
          'peak_rss_mean_mib':statistics.mean(r['peak_rss_bytes']/1048576 for r in a),
          'building_scope_mean_s':statistics.mean((scope(r,'gradient.building')+scope(r,'gradient.building_resume'))/1e9 for r in a),
          'resource_scope_mean_s':statistics.mean(scope(r,'gradient.resource')/1e9 for r in a),
          'loop_max_median_ms':statistics.median(scope(r,'loop.work','max_ns')/1e6 for r in a),
          'save_scope_mean_ms':statistics.mean(scope(r,'save.serialize','mean_ns')/1e6 for r in a),
          'save_scope_max_median_ms':statistics.median(scope(r,'save.serialize','max_ns')/1e6 for r in a),
          'save_scope_max_worst_ms':max(scope(r,'save.serialize','max_ns')/1e6 for r in a),
          'save_calls_per_run':[int(scope(r,'save.serialize','calls')) for r in a],
          'wall_cpu_ratios':[r['wall_s']/r['cpu_s'] for r in a],
          'involuntary_switches_mean':statistics.mean(r['involuntary_switches'] for r in a),
          'major_faults_total':sum(r['major_faults'] for r in a)}
    if c['save_every']:
        result['save_mean_lazy_vs_eager']=paired(blocks,'eager','lazy',lambda r:scope(r,'save.serialize','mean_ns'))
        result['save_mean_extra_ms']=difference(blocks,'eager','lazy',lambda r:scope(r,'save.serialize','mean_ns')/1e6)
    summary.append(result)
(OUT/'analysis.json').write_text(json.dumps(summary,indent=2)+'\n')
print(f'{len(rows)}/{sum(c["repetitions"] for c in plan["cases"])*3} measured runs; {sum(s["blocks"] for s in summary)}/{len(plan["blocks"])} complete blocks')
for s in summary:
    def fmt(key):
        p=s[key];ci=p['ci95_percent'];return f"{p['saving_percent']:+.1f}%"+(f" [{ci[0]:+.1f}, {ci[1]:+.1f}]" if ci else '')
    print(s['case'],f"n={s['blocks']}/{s['planned_blocks']}",'refactor',fmt('refactor_vs_base'),'lazy',fmt('lazy_vs_eager'),'net',fmt('lazy_vs_base'))
