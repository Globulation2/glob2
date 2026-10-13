from pathlib import Path
import json,statistics
import numpy as np
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
ROOT=Path(__file__).resolve().parents[1]
c=json.loads((ROOT/'experiment.json').read_text());ss=json.loads((ROOT/'summary.json').read_text());acc=json.loads((ROOT/'analysis/accounting.json').read_text());rng=np.random.default_rng(20261011)
prev=None;out=[]
for s in c['snapshots']:
 p=ROOT/'snapshots'/(s['sample_date']+'-'+s['commit'][:9]);rs=[json.loads((p/'runs'/str(seed)/'result.json').read_text()) for seed in c['seeds']]
 row={'date':s['sample_date'],'pairs':{}}
 if prev:
  boot=rng.integers(0,32,size=(10000,32))
  for key in ['ticks_per_second','cpu_seconds_per_tick','wall_seconds','peak_rss_kib']:
   a=np.array([x[key] for x in prev]);b=np.array([x[key] for x in rs]);ratios=(b[boot].mean(axis=1)/a[boot].mean(axis=1)-1)*100
   row['pairs'][key]={'change_percent':float((b.mean()/a.mean()-1)*100),'bootstrap95_percent':np.percentile(ratios,[2.5,97.5]).tolist()}
 out.append(row);prev=rs
(ROOT/'analysis/interval-statistics.json').write_text(json.dumps(out,indent=2)+'\n')
fig,axes=plt.subplots(1,3,figsize=(15,4.8),layout='constrained');x=range(len(ss));labels=[s['sample_date'][5:] for s in ss]
axes[0].plot(x,[s['mean_ticks_per_second'] for s in ss],'-o',color='#126a6b');axes[0].set_title('Delivered throughput');axes[0].set_ylabel('ticks/second')
axes[1].bar(x,[r['user_cpu']/s['mean_ticks']*1e6 for r,s in zip(acc,ss)],color='#126a6b',label='User CPU');axes[1].bar(x,[r['system_cpu']/s['mean_ticks']*1e6 for r,s in zip(acc,ss)],bottom=[r['user_cpu']/s['mean_ticks']*1e6 for r,s in zip(acc,ss)],color='#c07a33',label='Kernel CPU');axes[1].set_title('CPU cost (total CPU / total ticks)');axes[1].set_ylabel('microseconds/tick');axes[1].legend(frameon=False)
axes[2].plot(x,[s['mean_peak_rss_kib']/1024 for s in ss],'-o',color='#7179aa');axes[2].set_title('Peak resident memory');axes[2].set_ylabel('Mean MiB/game')
for ax in axes:
 ax.set_xticks(list(x),labels,rotation=55,ha='right');ax.grid(axis='y',alpha=.2);ax.spines[['top','right']].set_visible(False);ax.set_ylim(bottom=0)
fig.suptitle('Historical upgrades: speed, CPU work and memory tell different stories',fontsize=16,fontweight='bold')
fig.savefig(ROOT/'analysis/evolution-diagnostics.png',dpi=160);fig.savefig(ROOT/'analysis/evolution-diagnostics.svg')
print('Paired statistics and diagnostic chart saved')
for r in out:
 if r['date'] in ['2026-09-20','2026-09-29','2026-10-08']:
  print(r)
