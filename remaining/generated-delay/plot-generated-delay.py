from pathlib import Path
import json
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
import matplotlib.ticker as mtick
import numpy as np
r=Path('artifacts/resource-growth/remaining/generated-delay');a=json.loads((r/'analysis.json').read_text());out=r/'charts';out.mkdir(exist_ok=True)
plt.rcParams.update({'font.family':'DejaVu Sans','font.size':11,'axes.spines.top':False,'axes.spines.right':False,'axes.edgecolor':'#a8b0b8','axes.labelcolor':'#24313c','text.color':'#24313c','xtick.color':'#52606b','ytick.color':'#52606b','savefig.facecolor':'white'})
materials=['wheat','wood','algae'];colors={'wheat':'#ae740a','wood':'#19704d','algae':'#286cba'};delays=[1,2,3,4,8,12,16]
fig,axes=plt.subplots(2,3,figsize=(15,9),layout='constrained')
for i,comp in enumerate(['master','delay8']):
 rows=[v for v in a['aggregates'] if v['ticks']==4096 and v['endpoint']=='normal' and v['comparator']==comp]
 lim=max(.15,max(abs(z) for v in rows for z in v['simultaneous_ci95'])*1.2)
 for j,material in enumerate(materials):
  ax=axes[i,j];series=sorted([v for v in rows if v['material']==material],key=lambda v:v['delay']);x=np.array([v['delay'] for v in series]);y=np.array([v['equal_generator_mean_percent'] for v in series]);lo=np.array([v['simultaneous_ci95'][0] for v in series]);hi=np.array([v['simultaneous_ci95'][1] for v in series]);c=colors[material]
  ax.axhline(0,color='#616c76',lw=1,ls='--');ax.axvline(8,color='#c8cfd5',lw=1,zorder=0)
  ax.fill_between(x,lo,hi,color=c,alpha=.18,label='Simultaneous 95% band');ax.plot(x,y,color=c,lw=2,marker='o',ms=5,label='Mean change')
  ax.set_ylim(-lim,lim);ax.set_xlim(.5,16.5);ax.set_xticks(delays);ax.tick_params(axis='x',labelrotation=35);ax.yaxis.set_major_formatter(mtick.FormatStrFormatter('%+.2f'))
  ax.grid(axis='y',alpha=.15);ax.set_title(material.capitalize(),fontweight='bold');ax.set_xlabel('Publication delay (ticks)')
  if j==0:ax.set_ylabel(('Versus delay 8' if comp=='delay8' else 'Versus current master')+'\nFinal stock change (%)')
fig.suptitle('How publication delay changes resource stocks\n',fontsize=20,fontweight='bold')
fig.supxlabel('240 real master-generated maps · 12 generator families × 20 seeds · 4,096 ticks · No harvesting\nEqual weight per generator. Shading: simultaneous 95% bands across material/delay comparisons within each row.\nFinal stocks include starting stocks; a small effect can still be distinguishable from zero. Master: aee20ae52.',fontsize=10)
for ext in ['png','svg','pdf']:fig.savefig(out/f'delay-overview.{ext}',dpi=170)
plt.close(fig)
# Show family variation and pointwise uncertainty for the widest contrast.
names=sorted(v['generator'] for v in a['coverage']);fig,axes=plt.subplots(1,3,figsize=(15,9),layout='constrained');ys=np.arange(len(names))
for ax,material in zip(axes,materials):
 for delay,offset,c,label in [(1,-.14,'#2677ad','Delay 1 vs 8'),(16,.14,'#bc592d','Delay 16 vs 8')]:
  rows=[next(v for v in a['results'] if v['generator']==name and v['material']==material and v['delay']==delay and v['ticks']==4096 and v['endpoint']=='normal' and v['comparator']=='delay8') for name in names]
  means=np.array([v['stock_percent'] for v in rows]);low=np.array([v['stock_ci95'][0] for v in rows]);high=np.array([v['stock_ci95'][1] for v in rows]);ax.errorbar(means,ys+offset,xerr=[np.maximum(0,means-low),np.maximum(0,high-means)],fmt='o',ms=4,c=c,capsize=2,lw=1.2,label=label)
 ax.axvline(0,color='#606d78',lw=1,ls='--');ax.set_yticks(ys,[n.replace('-',' ').title() for n in names]);ax.set_ylim(len(names)+.4,-.7);ax.set_title(material.capitalize(),fontweight='bold');ax.set_xlabel('Final stock change versus delay 8 (%)');ax.grid(axis='x',alpha=.15);ax.legend(loc='lower right',fontsize=9)
fig.suptitle('The same delay change can affect map families differently',fontsize=18,fontweight='bold')
fig.supxlabel('20 paired seeds per generator · 4,096 ticks · Pointwise 95% bootstrap intervals (not multiplicity adjusted)\nAll inputs came from current master; these contrasts isolate delay within the snapshot engine.',fontsize=10)
for ext in ['png','svg','pdf']:fig.savefig(out/f'map-family-effects.{ext}',dpi=170)
plt.close(fig)
print(out)
