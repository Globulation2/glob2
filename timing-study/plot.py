from pathlib import Path
import json
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt

out=Path(__file__).resolve().parent
rows=json.loads((out/'analysis.json').read_text())
names={'arena128-2':'Arena 128² · 2 players', 'arena256-2':'Arena 256² · 2 players',
       'arena512-4':'Arena 512² · 4 players', 'lakes256-4':'Lakes 256² · 4 players',
       'arena256-seed73':'Arena 256² · new seed, 4 players', 'lakes256-seed131':'Lakes 256² · new seed, 4 players',
       'lakes256-checkpoints':'Lakes 256² · four checkpoints'}
plt.rcParams.update({'font.family':'DejaVu Sans','font.size':10,'axes.spines.top':False,'axes.spines.right':False})
fig,axes=plt.subplots(1,2,figsize=(12,6.2),sharey=True,gridspec_kw={'width_ratios':[1.4,1]},layout='constrained')
for ax,key,title,color in zip(axes,['lazy_vs_base','refactor_vs_base'],['Lazy mode versus original baseline','PR with lazy mode disabled'],['#176e8e','#696a74']):
    ax.axvline(0,color='#a34442',linewidth=1)
    for i,row in enumerate(rows):
        r=row[key];mean=r['saving_percent'];lo,hi=r['ci95_percent']
        ax.scatter(r['block_savings_percent'],[i]*r['n'],s=14,color=color,alpha=.25,zorder=2)
        ax.errorbar(mean,i,xerr=[[mean-lo],[hi-mean]],fmt='o',markersize=5,color=color,capsize=3,zorder=3)
    ax.set_title(title,fontsize=12,pad=15)
    ax.set_xlabel('CPU time saved (%) · positive is better',labelpad=12)
    ax.grid(axis='x',alpha=.15)
axes[0].set_yticks(range(len(rows)),[f"{names[r['case']]}  (n={r['blocks']})" for r in rows])
axes[0].invert_yaxis()
fig.suptitle('Lazy building gradients: repeated, balanced comparisons',fontsize=15,fontweight='bold')
fig.get_layout_engine().set(rect=(0, .055, 1, .945))
fig.text(.02,.012,'Dots: paired block results. Bars: approximate 95% confidence intervals on paired log ratios. Apple M3, AC power; headless CPU time.',fontsize=8,color='#555555')
fig.savefig(out/'cpu-comparison.png',dpi=180,bbox_inches='tight')
fig.savefig(out/'cpu-comparison.svg',bbox_inches='tight')
