from pathlib import Path
import json,csv,collections
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
import numpy as np
R=Path(__file__).resolve().parent;D=json.loads((R/'RESULTS.json').read_text());rows=[x for x in csv.DictReader((R/'paired-rows.csv').open()) if x['eligible']=='True' and int(x['family'])==0 and int(x['movement'])<8];groups=list(D['primary_per_map']);names=['4FFA · polder','2v2 · islands','8FFA · fjord-continent'];colors=['#0072B2','#D55E00','#009E73']
plt.rcParams.update({'font.size':10,'axes.spines.top':False,'axes.spines.right':False,'savefig.dpi':180})
fig,axes=plt.subplots(1,3,figsize=(12,4.7),sharex=True,sharey=True)
for ax,g,name,color in zip(axes,groups,names,colors):
 pop=[x for x in rows if x['run_group']==g];counts=collections.Counter((int(x['B32']),int(x['B128'])) for x in pop);ax.axvspan(24,40,color='#E69F00',alpha=.12);ax.plot([-10,40],[-50,200],color='#888888',ls='--',lw=1,label='5× steady-rate reference');ax.axvline(32,color='#E69F00',ls=':',lw=1);ax.axhline(160,color='#E69F00',ls=':',lw=1);ax.axhline(0,color='#BBBBBB',lw=.6);ax.axvline(0,color='#BBBBBB',lw=.6)
 for (x,y),n in counts.items():
  ax.scatter(x,y,s=22+22*n,color=color,alpha=.8,edgecolor='white',linewidth=.5,zorder=3)
  if n>1:ax.annotate('×'+str(n),(x,y),xytext=(5,5),textcoords='offset points',fontsize=8)
 ax.set_xlim(-11,42);ax.set_ylim(-58,207);ax.set_xlabel('B32: offsets 8–31');ax.set_title(name+'\n'+str(len(pop))+' pairs; near-32 band empty',fontsize=10);ax.grid(alpha=.12)
axes[0].set_ylabel('B128: offsets 8–127 (direction-cost units)');axes[0].legend(loc='upper left',fontsize=8);fig.suptitle('Paired natural-ground Materials: no observations near the old cutoff',fontsize=13);fig.text(.5,.015,'Identical capture/query counterfactuals. Shading = predeclared B32 band [24,40]; 160 is only the constant-accrual reference.',ha='center',fontsize=8);fig.tight_layout(rect=[0,.04,1,.92])
for ext in ['png','pdf']:fig.savefig(R/('paired-benefit.'+ext))
plt.close(fig)
fig,axes=plt.subplots(1,2,figsize=(10,4.6));x=np.arange(3);width=.34
for ax,key,label in [(axes[0],'B','Signed direction benefit / weighted episode'),(axes[1],'Q','Queries / weighted episode')]:
 for offset,h,color in [(-width/2,'32','#0072B2'),(width/2,'128','#E69F00')]:
  values=[D['primary_per_map'][g]['distributions'][key+h]['mean'] for g in groups];ax.bar(x+offset,values,width,label='H'+h,color=color)
 ax.set_xticks(x);ax.set_xticklabels(names,rotation=15,ha='right');ax.axhline(0,color='black',lw=.7);ax.set_ylabel(label);ax.grid(axis='y',alpha=.15);ax.legend()
fig.suptitle('Map heterogeneity: longer query exposure can add or cancel benefit',fontsize=13);fig.text(.5,.01,'Only three maps. Episode hierarchies weight maps equally; these bars are descriptive means, not whole-game totals.',ha='center',fontsize=8);fig.tight_layout(rect=[0,.03,1,.92])
for ext in ['png','pdf']:fig.savefig(R/('map-heterogeneity.'+ext))
