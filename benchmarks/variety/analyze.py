import csv,json,sys
from pathlib import Path
import numpy as np
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
ROOT=Path(sys.argv[1]) if len(sys.argv)>1 else Path(__file__).resolve().parent
WORKLOADS=['small-duel','archipelago','large-eleven'];POLICIES=['capped','team4'];rows=[]
def q(s,p):return float(np.quantile(s,p,method='lower'))
for workload in WORKLOADS:
 for seed in range(42,74):
  for participants in [1,4]:
   for mode in POLICIES:
    path=ROOT/workload/f'seed-{seed}-{mode}-p{participants}';v=json.loads((path/'game/result.json').read_text());run=json.loads((path/'run.json').read_text());assert run['exit_code']==0 and not run['compiler_seen']
    s=np.fromfile(path/'tick-ns.u64',dtype=np.uint64).astype(float)/1e6;n=len(s);assert n==v['benchmark_measured_ticks'] and np.any(np.diff(s)<0)
    if mode=='capped':assert v['building_gradient_max_batch']<=4
    else:assert v['building_gradient_max_team_batch']<=4
    hist=np.array(v['building_gradient_queue_delay_histogram']);count=int(hist.sum());assert count==v['building_gradient_queue_delay_samples']
    def hq(p):return int(np.searchsorted(np.cumsum(hist),max(1,int(np.ceil(count*p))))) if count else 0
    diffs=np.abs(np.diff(s));row={'workload':workload,'seed':seed,'participants':participants,'mode':mode,'ticks':n,'mean_ms':float(s.mean()),'cpu_ms_per_1000_ticks':v['benchmark_run_cpu_ns']/n/1000,'peak_rss_mib':run['peak_rss_bytes']/1024**2,'p99_ms':q(s,.99),'p999_ms':q(s,.999),'adjacent_change_p99_ms':q(diffs,.99),'adjacent_change_mean_ms':float(diffs.mean()),'gradient_jobs_per_1000_ticks':v['building_gradient_jobs']/n*1000,'gradient_cpu_ms_per_1000_ticks':v['building_gradient_worker_cpu_ns']/n/1000,'max_batch':v['building_gradient_max_batch'],'max_pending':v['building_gradient_max_pending_observed'],'max_team_batch':v['building_gradient_max_team_batch'],'queue_extra_mean_ticks':v['building_gradient_queue_delay_total']/count if count else 0,'queue_extra_p95_ticks':hq(.95),'queue_extra_p99_ticks':hq(.99),'queue_extra_max_ticks':v['building_gradient_queue_delay_max'],'queue_delayed_percent':100*v['building_gradient_delayed_requests']/count if count else 0,'queue_samples':count,'synchronous_overflow':v['building_gradient_synchronous_by_reason']['overflow'],'checksum':v['finalChecksum'],'initial_checksum':v['initialChecksum'],'final_units':sum(t['units'] for t in v['teams']),'final_buildings':sum(t['buildings']+t['sites'] for t in v['teams'])}
    for width in [8,32]:
     c=np.r_[0,np.cumsum(s)];rolling=c[width:]-c[:-width];row[f'window_{width}_p99_ms']=q(rolling,.99)
    for threshold in [1,5,33.333333]:
     high=s>threshold;starts=np.flatnonzero(high & ~np.r_[False,high[:-1]]);ends=np.flatnonzero(high & ~np.r_[high[1:],False]);lengths=ends-starts+1
     row[f'over_{threshold:g}ms_per_million']=float(high.mean()*1e6);row[f'clusters_ge2_over_{threshold:g}ms_per_million']=float((lengths>=2).sum()/n*1e6)
    # Descriptive late-half metrics; compare only complete matching-seed windows later.
    late=s[16384:32768]
    row['late_ticks']=len(late);row['late_mean_ms']=float(late.mean()) if len(late) else None;row['late_p99_ms']=q(late,.99) if len(late) else None;row['late_adjacent_change_p99_ms']=q(np.abs(np.diff(late)),.99) if len(late)>1 else None
    rows.append(row)
for workload in WORKLOADS:
 for seed in range(42,74):
  for mode in POLICIES:
   pair=[r for r in rows if r['workload']==workload and r['seed']==seed and r['mode']==mode];assert pair[0]['checksum']==pair[1]['checksum']
  for p in [1,4]:assert len({r['initial_checksum'] for r in rows if r['workload']==workload and r['seed']==seed and r['participants']==p})==1
summary={'runs':len(rows),'seeds_per_workload':32,'same_mode_participant_checksums_match':True,'workloads':{}}
indices=np.random.default_rng(20261016).integers(0,32,(10000,32))
for workload in WORKLOADS:
 output={}
 for participants in [1,4]:
  group=[r for r in rows if r['workload']==workload and r['participants']==participants and r['mode']=='team4'];base=[r for r in rows if r['workload']==workload and r['participants']==participants and r['mode']=='capped'];metrics={}
  for key in group[0]:
   if key in ['workload','seed','participants','mode','checksum','initial_checksum'] or key.startswith('late_'):continue
   if any(r[key] is None for r in group+base):continue
   a=np.array([r[key] for r in base],float);b=np.array([r[key] for r in group],float);ma=a.mean();mb=b.mean();da=a[indices].mean(axis=1);db=b[indices].mean(axis=1);valid=da!=0;changes=100*(db[valid]/da[valid]-1)
   metrics[key]={'capped_mean':float(ma),'team4_mean':float(mb),'change_percent':float(100*(mb/ma-1)) if ma else None,'paired_bootstrap_95_percent':np.quantile(changes,[.025,.975]).tolist() if len(changes) else None,'team4_lower_seeds':int((b<a).sum()),'capped_range':[float(a.min()),float(a.max())],'team4_range':[float(b.min()),float(b.max())]}
  complete=[(a,b) for a,b in zip(base,group) if a['late_ticks']==b['late_ticks']==16384]
  late_metrics={}
  if len(complete)>=2:
   ix=np.random.default_rng(20261016).integers(0,len(complete),(10000,len(complete)))
   for key in ['late_mean_ms','late_p99_ms','late_adjacent_change_p99_ms']:
    a=np.array([a[key] for a,b in complete]);b=np.array([b[key] for a,b in complete]);ma=a.mean();mb=b.mean();changes=100*(b[ix].mean(axis=1)/a[ix].mean(axis=1)-1)
    late_metrics[key]={'capped_mean':float(ma),'team4_mean':float(mb),'change_percent':float(100*(mb/ma-1)),'paired_bootstrap_95_percent':np.quantile(changes,[.025,.975]).tolist()}
  output[str(participants)]={'late_half_metrics_matched_complete_pairs':late_metrics,'metrics':metrics,'ticks':{m:sum(r['ticks'] for r in rows if r['workload']==workload and r['participants']==participants and r['mode']==m) for m in POLICIES},'complete_late_half_seeds':[a['seed'] for a,b in zip(base,group) if a['late_ticks']==b['late_ticks']==16384]}
 summary['workloads'][workload]=output
(ROOT/'summary.json').write_text(json.dumps(summary,indent=2))
with (ROOT/'per-game.csv').open('w') as f:
 w=csv.DictWriter(f,fieldnames=rows[0]);w.writeheader();w.writerows(rows)
fig,axes=plt.subplots(1,3,figsize=(14,4.8))
labels=['2-team duel','5-team islands','11-team large']
for ax,key,title in [(axes[0],'cpu_ms_per_1000_ticks','Total CPU change'),(axes[1],'adjacent_change_mean_ms','Mean tick-to-tick variability change')]:
 x=np.arange(3)
 for participants,offset,color in [(1,-.16,'#3269ae'),(4,.16,'#dc751b')]:
  metrics=[summary['workloads'][w][str(participants)]['metrics'][key] for w in WORKLOADS]
  vals=np.array([m['change_percent'] for m in metrics]);lo=np.array([m['paired_bootstrap_95_percent'][0] for m in metrics]);hi=np.array([m['paired_bootstrap_95_percent'][1] for m in metrics])
  ax.errorbar(x+offset,vals,yerr=np.array([vals-lo,hi-vals]),fmt='o',capsize=4,color=color,label=str(participants)+' participant'+('s' if participants>1 else ''))
 ax.axhline(0,color='#555555',linewidth=1);ax.set(title=title,ylabel='Change from global four (%)',xticks=x,xticklabels=labels);ax.grid(axis='y',alpha=.2);ax.legend(frameon=False)
ax=axes[2];x=np.arange(3)
for mode,offset,color in [('capped',-.18,'#3269ae'),('team4',.18,'#dc751b')]:
 vals=[summary['workloads'][w]['1']['metrics']['queue_extra_mean_ticks'][mode+'_mean'] for w in WORKLOADS]
 ax.bar(x+offset,vals,width=.36,label='Global 4' if mode=='capped' else 'Four per team',color=color)
ax.set(title='Mean extra queue wait',ylabel='Ticks',xticks=x,xticklabels=labels);ax.grid(axis='y',alpha=.2);ax.set_axisbelow(True);ax.legend(frameon=False)
fig.suptitle('Four per team: broader matches reveal the cost',fontsize=15)
fig.text(.5,.01,'32 paired seeds per workload · 95% paired bootstrap intervals · same Mac, release build, headless execution',ha='center',fontsize=10)
fig.tight_layout(rect=(0,.05,1,.94));fig.savefig(ROOT/'variety.png',dpi=150)
print(json.dumps(summary,indent=2))
