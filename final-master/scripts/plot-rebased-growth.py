from pathlib import Path
import json
import numpy as np
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
b=Path('artifacts/resource-growth/final-master');d=json.loads((b/'paired/summary.json').read_text())['results'];out=b/'charts';out.mkdir(exist_ok=True)
labels={'dense':'Dense · 256²','multi':'Multi-material · 512²','ai512':'AI · 512²','disabled512':'Growth disabled · 512²','sparse':'Sparse · 128²','saturated':'Saturated · 256²','harvested':'Harvesting · 256²','fragmented':'Fragmented · 512²'}
plt.rcParams.update({'font.family':'DejaVu Sans','font.size':11,'axes.spines.top':False,'axes.spines.right':False})
fig,axes=plt.subplots(1,2,figsize=(13,6),sharey=True)
rs=d['master-vs-shared8'];names=list(rs);y=np.arange(len(names))
for ax,key,title,color in zip(axes,['tps_gain','cpu_reduction'],['Engine throughput increase','Engine CPU reduction'],['#157f83','#6758a5']):
 values=np.array([rs[n][key]['median']*100 for n in names]);lo=np.array([rs[n][key]['ci95'][0]*100 for n in names]);hi=np.array([rs[n][key]['ci95'][1]*100 for n in names]);ax.axvline(0,color='#555',lw=1);ax.errorbar(values,y,xerr=[values-lo,hi-values],fmt='o',color=color,capsize=4,markersize=6);ax.set_title(title);ax.set_xlabel('Change versus master (%) · positive is better');ax.grid(axis='x',alpha=.2);ax.set_yticks(y,[labels[n] for n in names]);ax.margins(x=.2)
 for i,v in enumerate(values):ax.annotate(f'{v:+.1f}%',(v,i),xytext=(0,-16),textcoords='offset points',ha='center',fontsize=9)
axes[0].set_ylim(len(names)-.1,-.65);fig.suptitle('Delay-8 shared growth after rebasing onto master',fontsize=17,y=.98)
fig.text(.5,.02,'1,024 ticks/run · four reserved physical cores · 10–30 pairs · bootstrap 95% intervals\nMaster 68aa1075 vs branch 61b6ff740 · intervals are per scenario; simulation trajectories can differ',ha='center',fontsize=10)
fig.tight_layout(rect=[0,.09,1,.95])
for ext in ['png','svg','pdf']:fig.savefig(out/f'performance-vs-master.{ext}',dpi=180,bbox_inches='tight')
