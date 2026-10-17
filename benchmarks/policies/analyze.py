import csv,json,sys
from pathlib import Path
import numpy as np
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
ROOT=Path(sys.argv[1]) if len(sys.argv)>1 else Path(__file__).resolve().parent
POLICIES=['capped','global8','global16','team2','team4','all'];LABELS=['Global 4','Global 8','Global 16','Team 2','Team 4','Uncapped']
rows=[]
for seed in range(42,74):
 for participants in [1,4]:
  for mode in POLICIES:
   path=ROOT/f'seed-{seed}-{mode}-p{participants}';v=json.loads((path/'game/result.json').read_text());run=json.loads((path/'run.json').read_text())
   assert run['exit_code']==0
   s=np.fromfile(path/'tick-ns.u64',dtype=np.uint64).astype(float)/1e6;n=len(s);assert n==v['benchmark_measured_ticks'] and np.any(np.diff(s)<0)
   if mode in ['capped','global8','global16']:assert v['building_gradient_max_batch']<={'capped':4,'global8':8,'global16':16}[mode]
   if mode in ['team2','team4']:assert v['building_gradient_max_team_batch']<={'team2':2,'team4':4}[mode]
   hist=np.array(v['building_gradient_queue_delay_histogram']);count=int(hist.sum());assert count==v['building_gradient_queue_delay_samples']
   if mode=='all':assert v['building_gradient_queue_delay_max']==0 and v['building_gradient_max_queued_after_boundary']==0
   def hq(p):return int(np.searchsorted(np.cumsum(hist),max(1,int(np.ceil(count*p))))) if count else 0
   diffs=np.abs(np.diff(s));row={'seed':seed,'participants':participants,'mode':mode,'ticks':n,'mean_ms':float(s.mean()),'cpu_ms_per_1000_ticks':v['benchmark_run_cpu_ns']/n/1000,'peak_rss_mib':run['peak_rss_bytes']/1024**2,'p99_ms':float(np.quantile(s,.99,method='lower')),'p999_ms':float(np.quantile(s,.999,method='lower')),'adjacent_change_p99_ms':float(np.quantile(diffs,.99,method='lower')),'adjacent_change_mean_ms':float(diffs.mean()),'gradient_jobs_per_1000_ticks':v['building_gradient_jobs']/n*1000,'gradient_cpu_ms_per_1000_ticks':v['building_gradient_worker_cpu_ns']/n/1000,'max_batch':v['building_gradient_max_batch'],'max_pending':v['building_gradient_max_pending_observed'],'max_team_batch':v['building_gradient_max_team_batch'],'queue_extra_mean_ticks':v['building_gradient_queue_delay_total']/count if count else 0,'queue_extra_p95_ticks':hq(.95),'queue_extra_p99_ticks':hq(.99),'queue_extra_max_ticks':v['building_gradient_queue_delay_max'],'queue_delayed_percent':100*v['building_gradient_delayed_requests']/count if count else 0,'queue_samples':count,'synchronous_overflow':v['building_gradient_synchronous_by_reason']['overflow'],'checksum':v['finalChecksum'],'initial_checksum':v['initialChecksum']}
   for width in [8,32]:
    c=np.r_[0,np.cumsum(s)];rolling=c[width:]-c[:-width];row[f'window_{width}_p99_ms']=float(np.quantile(rolling,.99,method='lower'))
   for threshold in [1,5,33.333333]:
    high=s>threshold;starts=np.flatnonzero(high & ~np.r_[False,high[:-1]]);ends=np.flatnonzero(high & ~np.r_[high[1:],False]);lengths=ends-starts+1
    row[f'over_{threshold:g}ms_per_million']=float(high.mean()*1e6)
    row[f'clusters_ge2_over_{threshold:g}ms_per_million']=float((lengths>=2).sum()/n*1e6)
   rows.append(row)
for seed in range(42,74):
 for mode in POLICIES:
  pair=[r for r in rows if r['seed']==seed and r['mode']==mode];assert pair[0]['checksum']==pair[1]['checksum']
 for p in [1,4]:assert len({r['initial_checksum'] for r in rows if r['seed']==seed and r['participants']==p})==1
summary={'runs':len(rows),'seeds':32,'same_mode_participant_checksums_match':True,'participants':{}}
indices=np.random.default_rng(20261016).integers(0,32,(10000,32))
for participants in [1,4]:
 results={}
 for mode in POLICIES:
  group=[r for r in rows if r['participants']==participants and r['mode']==mode];base=[r for r in rows if r['participants']==participants and r['mode']=='capped'];metrics={}
  for key in group[0]:
   if key in ['seed','participants','mode','ticks','checksum','initial_checksum']:continue
   a=np.array([r[key] for r in base],float);b=np.array([r[key] for r in group],float);ma=a.mean();mb=b.mean();da=a[indices].mean(axis=1);db=b[indices].mean(axis=1);valid=da!=0;changes=100*(db[valid]/da[valid]-1)
   metrics[key]={'mean':float(mb),'change_vs_global4_percent':float(100*(mb/ma-1)) if ma else None,'paired_bootstrap_95_percent':np.quantile(changes,[.025,.975]).tolist() if len(changes) else None,'lower_than_global4_seeds':int((b<a).sum()),'range':[float(b.min()),float(b.max())]}
  results[mode]={'ticks':sum(r['ticks'] for r in group),'metrics':metrics}
 summary['participants'][str(participants)]=results
(ROOT/'summary.json').write_text(json.dumps(summary,indent=2))
with (ROOT/'per-game.csv').open('w') as f:
 w=csv.DictWriter(f,fieldnames=rows[0]);w.writeheader();w.writerows(rows)
fig,axes=plt.subplots(2,2,figsize=(12,8))
for i,p in enumerate([4,1]):
 for j,(key,title) in enumerate([('adjacent_change_p99_ms','P99 change between consecutive tick times (ms)'),('queue_extra_mean_ticks','Mean extra queue delay (ticks)')]):
  ax=axes[i,j];values=[summary['participants'][str(p)][m]['metrics'][key]['mean'] for m in POLICIES]
  ax.bar(LABELS,values,color=['#3269ae','#4d8bc3','#7ba8cf','#bd941a','#d77a28','#c54a4a']);ax.set(title=f'{p} compute participant'+('s' if p>1 else '')+' · '+title);ax.grid(axis='y',alpha=.2);ax.set_axisbelow(True)
fig.suptitle('Gradient admission policies: jitter versus freshness',fontsize=15)
fig.text(.5,.01,'32 seeds per policy/configuration · same Mac/map · release build · headless unrestricted execution',ha='center',fontsize=10)
fig.tight_layout(rect=(0,.04,1,.95));fig.savefig(ROOT/'policies.png',dpi=150)
print(json.dumps(summary,indent=2))
