#!/usr/bin/env python3
"""Recompute saved historical means and 60 scaling cells; no game/build launch.

Run from an extracted evidence package:
 python3 thread-scaling/recompute-scalars.py --root PATH_TO_PACKAGE
Reads only CSV/JSON, writes a requested output or prints a compact verification.
Warmups stay excluded; layouts, CPU budgets and GPU settings never pool.
"""
import argparse,csv,json,math,random,statistics
from pathlib import Path

def stats(strata):
 ratio=math.exp(statistics.mean(statistics.mean(v) for v in strata))
 rng=random.Random(7134);values=[]
 for _ in range(4000):
  sampled=rng.choices(strata,k=len(strata))
  values.append(math.exp(sum(sum(rng.choices(v,k=len(v)))/len(v) for v in sampled)/len(sampled)))
 values.sort();ci=[values[100],values[3899]]
 return dict(ratio=ratio,percent_change=100*(ratio-1),ci95=ci,percent_ci95=[100*(v-1) for v in ci])

def main():
 p=argparse.ArgumentParser();p.add_argument('--root',type=Path,default=Path(__file__).resolve().parent.parent);p.add_argument('--output',type=Path);a=p.parse_args()
 report={'reads_saved_scalars_only':True,'new_native_runs':0,'history':{},'thread_scaling':{}}
 failures=[]
 for name in ['historical-playground','historical-oazis']:
  rows=list(csv.DictReader((a.root/name/'runs.csv').open()));original=json.loads((a.root/name/'summary.json').read_text());summary=[]
  for g in original:
   rr=[r for r in rows if r['commit']==g['commit']];winners=[r for r in rr if r['status']=='completed']
   x=dict(commit=g['commit'],sample_date=g['sample_date'],attempts=len(rr),completed=len(winners),timeouts=sum(r['status']=='timeout' for r in rr))
   for key in ['ticks','wall_seconds','peak_rss_kib','total_cpu_seconds','cpu_seconds_per_tick','ticks_per_second','e2e_ticks_per_second']:
    vals=[float(r[key]) for r in winners if r[key] not in ['', 'None', 'null']];value=statistics.mean(vals) if vals else None;x['mean_'+key]=value
    expected=g.get('mean_'+key)
    if value is None and expected is None:continue
    if value is None or expected is None or not math.isclose(value,expected,rel_tol=1e-12,abs_tol=1e-12):failures.append(f'{name}/{g["commit"]}/{key}')
   summary.append(x)
  report['history'][name]=summary
 rows=list(csv.DictReader((a.root/'thread-scaling/scaling-scalars.csv').open()));measured=[r for r in rows if r['warmup']=='0'];summary=json.loads((a.root/'thread-scaling/scaling-summary.json').read_text())
 assert len(rows)==480 and len(measured)==360
 report['thread_scaling']['observations']=480;report['thread_scaling']['warmups_excluded']=120;report['thread_scaling']['measured']=360;report['thread_scaling']['groups']={}
 for label,g in summary['groups'].items():
  c=g['context'];rr=[r for r in measured if all(str(r[k])==str(c[k]) for k in ['budget','layout','gpu','requested_count'])];seed_ids=list(dict.fromkeys(r['seed'] for r in rr));out=dict(context=c,metrics={})
  assert len(rr)==6 and len(seed_ids)==2
  for m in summary['metrics']:
   vals=[float(r[m]) for r in rr];strata=[]
   for seed in seed_ids:
    ss=[r for r in rr if r['seed']==seed];logs=[]
    for r in ss:
     refs=[v for v in measured if v['budget']==r['budget'] and v['layout']==r['layout'] and v['gpu']==r['gpu'] and v['seed']==seed and v['repeat']==r['repeat'] and v['requested_count']=='1'];assert len(refs)==1
     ref=float(refs[0][m]);value=float(r[m])
     if ref>0 and value>0:logs.append(math.log(value/ref))
    strata.append(logs)
   z=stats(strata) if all(len(v)==3 for v in strata) else dict(ratio=None,percent_change=None,ci95=None,percent_ci95=None)
   z['absolute_mean']=statistics.mean(vals);out['metrics'][m]=z
   expected=g['metrics'][m]
   for k in ['ratio','percent_change']:
    if z[k] is None and expected.get(k) is None:continue
    if z[k] is None or expected.get(k) is None or not math.isclose(z[k],expected[k],rel_tol=1e-12,abs_tol=1e-12):failures.append(label+'/'+m+'/'+k)
   if z['percent_ci95'] is not None:
    if any(not math.isclose(x,y,rel_tol=1e-12,abs_tol=1e-12) for x,y in zip(z['percent_ci95'],expected['percent_ci95'])):failures.append(label+'/'+m+'/CI')
  report['thread_scaling']['groups'][label]=out
 report['verification']=dict(passed=not failures,mismatches=failures,historical_means_match=not any(v.startswith('historical-') for v in failures),scaling_point_estimates_and_bootstrap_match=not any(not v.startswith('historical-') for v in failures),scope='Saved scalar recalculation only, not a new performance or determinism experiment; two seeds x three paired blocks remain descriptive')
 if a.output:a.output.write_text(json.dumps(report,indent=2)+'\n')
 print(json.dumps(report['verification'],indent=2));return 1 if failures else 0
if __name__=='__main__':raise SystemExit(main())
