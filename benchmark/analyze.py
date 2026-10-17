import csv,json,sys
from pathlib import Path
import numpy as np
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
ROOT=Path(sys.argv[1]) if len(sys.argv)>1 else Path(__file__).resolve().parent
exp=json.loads((ROOT/'experiment.json').read_text());rows=[]
q=lambda a,p:float(np.quantile(a,p,method='lower'))
for w in exp['workloads']:
 for seed in exp['seeds']:
  for n in exp['participants']:
   for mode in exp['policies']:
    path=ROOT/w['name']/f'seed-{seed}-{mode}-p{n}'
    if not(path/'game/result.json').exists():continue
    v=json.loads((path/'game/result.json').read_text());run=json.loads((path/'run.json').read_text());assert run['exit_code']==0 and not run['host_contention_seen']
    s=np.fromfile(path/'tick-ns.u64',dtype=np.uint64).astype(float)/1e6;count=len(s);assert count==v['benchmark_measured_ticks']
    d=np.abs(np.diff(s));c=np.r_[0,np.cumsum(s)]
    row=dict(total_run_cpu_s=v['benchmark_run_cpu_ns']/1e9,elapsed_s=run['elapsed_seconds'],workload=w['name'],seed=seed,participants=n,mode=mode,ticks=count,cpu_ms_per_1000_ticks=v['benchmark_run_cpu_ns']/count/1000,mean_tick_ms=float(s.mean()),p99_tick_ms=q(s,.99),p999_tick_ms=q(s,.999),adjacent_change_mean_ms=float(d.mean()),adjacent_change_p99_ms=q(d,.99),window8_p99_ms=q(c[8:]-c[:-8],.99),over_1ms_per_million=float((s>1).mean()*1e6),over_5ms_per_million=float((s>5).mean()*1e6),rss_mib=run['peak_rss_bytes']/2**20,final_units=sum(t['units'] for t in v['teams']),final_buildings=sum(t['buildings']+t['sites'] for t in v['teams']),checksum=v['finalChecksum'],initial_checksum=v['initialChecksum'],scheduled_building_jobs_per_1000_ticks=v['building_gradient_jobs']/count*1000,building_synchronous_per_1000_ticks=v['building_gradient_synchronous']/count*1000)
    probe=v['hiring_gradient_probe']
    row['all_building_builds_per_1000_ticks']=(v['building_gradient_jobs']+v['building_gradient_synchronous']+probe['private_builds'])/count*1000
    for name,value in probe.items():
     if name=='mode':continue
     row[(name[:-3]+'_ms_per_1000_ticks') if name.endswith('_ns') else name+'_per_1000_ticks']=value/count/1000 if name.endswith('_ns') else value/count*1000
    ground=probe['shortlisted_ground'];row['shortlisted_missing_percent']=100*probe['shortlisted_missing']/ground if ground else 0;row['shortlisted_unsettled_percent']=100*probe['shortlisted_unsettled']/ground if ground else 0
    pub=probe['published_ground'];row['published_missing_percent']=100*probe['published_missing']/pub if pub else 0;row['published_unsettled_percent']=100*probe['published_unsettled']/pub if pub else 0;row['published_stale_percent']=100*probe['published_stale']/pub if pub else 0;row['published_resolved_percent']=100-row['published_missing_percent']-row['published_unsettled_percent']
    primary=probe['published_primary_ground'];row['primary_missing_percent']=100*probe['published_primary_missing']/primary if primary else 0;row['primary_unsettled_percent']=100*probe['published_primary_unsettled']/primary if primary else 0
    row['private_discard_percent']=100*probe['discarded']/probe['private_builds'] if probe['private_builds'] else 0
    rows.append(row)
assert rows
with (ROOT/'per-game.csv').open('w',newline='') as f:
 out=csv.DictWriter(f,rows[0].keys());out.writeheader();out.writerows(rows)
rng=np.random.default_rng(20261017);summary={};exclude={'workload','seed','participants','mode','checksum','initial_checksum'}
for w in exp['workloads']:
 for n in exp['participants']:
  part=[r for r in rows if r['workload']==w['name'] and r['participants']==n];by={(r['seed'],r['mode']):r for r in part}
  seeds=[s for s in exp['seeds'] if all((s,m) in by for m in exp['policies'])]
  if not seeds:continue
  result={'paired_seeds':seeds,'games_per_mode':len(seeds),'comparisons':{}}
  for mode,reference,comparison in [('shortlist','baseline','shortlist'),('top','baseline','top'),('top','shortlist','top-vs-full')]:
   metrics={}
   indices=rng.integers(0,len(seeds),(10000,len(seeds)))
   for metric in rows[0]:
    if metric in exclude:continue
    a=np.array([by[s,reference][metric] for s in seeds],dtype=float);b=np.array([by[s,mode][metric] for s in seeds],dtype=float)
    diff=b-a;delta=float(diff.mean());boots=diff[indices].mean(axis=1);ci=np.quantile(boots,[.025,.975]).tolist()
    base=float(a.mean());other=float(b.mean());pct=(other/base-1)*100 if base else None
    # Ratio of means, paired resampling, not mean of per-game ratios.
    den=a[indices].mean(axis=1);valid=den>0
    pci=np.quantile((b[indices].mean(axis=1)[valid]/den[valid]-1)*100,[.025,.975]).tolist() if valid.any() else None
    metrics[metric]={'baseline_mean':base,'variant_mean':other,'difference':delta,'difference_ci95':ci,'percent_change':pct,'percent_change_ci95':pci}
   result['comparisons'][comparison]=metrics
  summary[f'{w["name"]}-p{n}']=result
(ROOT/'summary.json').write_text(json.dumps(summary,indent=2))
fig,axes=plt.subplots(2,2,figsize=(12,7.5));axes=axes.ravel()
keys=['cpu_ms_per_1000_ticks','owner_cpu_ms_per_1000_ticks','pending_distances_per_1000_ticks','p99_tick_ms']
titles=['Total CPU per executed tick','CPU extending gradients in live queries','Distance queries waiting for a field','99th-percentile tick duration']
labels=list(summary)
pretty={'holiday-p1':'4 teams\n1 participant','holiday-p4':'4 teams\n4 participants','large-eleven-p1':'11 teams\n1 participant','large-eleven-p4':'11 teams\n4 participants'}
for ax,key,title in zip(axes,keys,titles):
 for k,mode in enumerate(exp['policies'][1:]):
  vals=[summary[label]['comparisons'][mode][key]['percent_change'] for label in labels]
  cis=[summary[label]['comparisons'][mode][key]['percent_change_ci95'] for label in labels]
  errors=np.array([[max(0,value-ci[0]) for value,ci in zip(vals,cis)],[max(0,ci[1]-value) for value,ci in zip(vals,cis)]])
  ax.bar(np.arange(len(labels))+(k-.5)*.36,vals,width=.36,label={'shortlist':'Prewarm existing shortlist','exact':'Initialize before ranking','top':'Prewarm primary candidates'}[mode],yerr=errors,capsize=3)
 ax.axhline(0,color='black',lw=.7);ax.set_title(title);ax.set_ylabel('% change from baseline');ax.set_xticks(np.arange(len(labels)),[pretty[x] for x in labels]);ax.grid(axis='y',alpha=.15);ax.set_axisbelow(True)
axes[0].legend(fontsize=9)
fig.suptitle('Hiring gradient preparation: 32 paired seeds per map',fontsize=14)
fig.text(.5,.015,'Bars: change in mean per-game metric. Error bars: paired bootstrap 95% intervals (exploratory).\nFrozen PR 1197 prototype; unpaced headless games on Apple M3. Policies change game evolution.',ha='center',fontsize=9)
fig.tight_layout(rect=(0,.065,1,.96));fig.savefig(ROOT/'comparison.png',dpi=170)
print(json.dumps(summary,indent=2))
