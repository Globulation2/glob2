"""Scientific exports of measured policy points; no interpolated experiments."""
import argparse,json
from pathlib import Path
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
p=argparse.ArgumentParser();p.add_argument('summary',type=Path);p.add_argument('--title',required=True);a=p.parse_args()
data=json.loads(a.summary.read_text());points=data['points'];assert len(points)==81
quantiles=[0,.25,.5,.65,.8,.9,.95,.99,1];labels=['Zero','p25','p50','p65','p80','p90','p95','p99','Full']
fig,axes=plt.subplots(2,3,figsize=(16,8),sharex=True,sharey='row',layout='constrained')
for column,delay in enumerate([2,4,8]):
 for workers,marker in [(1,'o'),(2,'s'),(4,'^')]:
  selected=sorted([r for r in points if r['delay']==delay and r['workers']==workers],key=lambda r:r['quantile'])
  assert [r['quantile'] for r in selected]==quantiles
  for row,metric in enumerate(['speed_ratio','cpu_ratio']):
   axis=axes[row,column];values=[(r['metrics'][metric]-1)*100 for r in selected]
   lower=[(r['ci95'][metric][0]-1)*100 for r in selected];upper=[(r['ci95'][metric][1]-1)*100 for r in selected]
   line=axis.plot(range(9),values,marker=marker,label=f'{workers} gradient worker'+('s' if workers!=1 else ''))[0]
   axis.fill_between(range(9),lower,upper,color=line.get_color(),alpha=.12)
 for row in range(2):
  axis=axes[row,column];axis.axhline(0,color='gray',linewidth=.8);axis.grid(axis='y',alpha=.2);axis.set_xticks(range(9),labels,rotation=45);axis.set_xlabel('Prebuild policy (predicted extension-cost percentile)')
 axes[0,column].set_title(f'Publication delay: {delay} ticks')
axes[0,0].set_ylabel('Ticks per second change vs original lazy (%)')
axes[1,0].set_ylabel('Engine CPU per tick change vs original lazy (%)')
axes[0,0].legend(fontsize=9)
fig.suptitle(a.title+'\nEqual-family paired averages; pointwise 95% whole-match bootstrap intervals',fontsize=13)
for ext in ['png','pdf']:fig.savefig(a.summary.with_suffix('.'+ext),dpi=160)
plt.close(fig)
fig,axes=plt.subplots(1,3,figsize=(16,5),sharex=True,sharey=True,layout='constrained')
for axis,delay in zip(axes,[2,4,8]):
 for workers,marker in [(1,'o'),(2,'s'),(4,'^')]:
  selected=[r for r in points if r['delay']==delay and r['workers']==workers]
  axis.scatter([(r['metrics']['cpu_ratio']-1)*100 for r in selected],[(r['metrics']['speed_ratio']-1)*100 for r in selected],marker=marker,label=f'{workers} worker'+('s' if workers!=1 else ''))
 axis.scatter([0],[0],color='black',marker='x',label='Original lazy')
 axis.axvline(0,color='gray',linewidth=.8);axis.axhline(0,color='gray',linewidth=.8);axis.grid(alpha=.2);axis.set_title(f'Delay: {delay} ticks');axis.set_xlabel('Additional engine CPU per tick (%)')
axes[0].set_ylabel('Ticks per second change (%)');axes[0].legend(fontsize=9)
fig.suptitle(a.title+' — measured CPU/speed tradeoffs',fontsize=13)
for ext in ['png','pdf']:fig.savefig(a.summary.with_name(a.summary.stem+'-tradeoffs').with_suffix('.'+ext),dpi=160)
