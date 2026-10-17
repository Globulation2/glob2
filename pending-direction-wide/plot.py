from pathlib import Path
import csv
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
BASE=Path(__file__).parent
rows=list(csv.DictReader(open(BASE/'per-case.csv')))
data={}
for r in rows:
 if r['subset']!='movement' or r['dedup']!='False' or int(r['n'])<100:continue
 d=data.setdefault(r['case'],{'case':r['case'],'source':r['source'],'n':int(r['n'])})
 v=int(r['variant']);d['direct' if v==0 else 'turns']=100*int(r['progress'])/int(r['n'])
 if v==1:d['wrong']=100*int(r['worse'])/int(r['n'])
a=sorted((d for d in data.values() if 'direct' in d and 'turns' in d),key=lambda d:d['turns'])
plt.rcParams.update({'font.size':10,'axes.spines.top':False,'axes.spines.right':False})
fig,(ax,bx)=plt.subplots(1,2,figsize=(11.5,4.6),layout='constrained')
colors={'generator':'#2577b8','bundled':'#df7c24'}
for source in colors:
 d=[x for x in a if x['source']==source]
 ax.scatter([x['direct'] for x in d],[x['turns'] for x in d],s=32,color=colors[source],label='Generated' if source=='generator' else 'Bundled',alpha=.8)
ax.plot([0,100],[0,100],color='#9da5ab',lw=1,ls='--')
if a:
 ax.set_xlim(max(0,min(d['direct'] for d in a)-7),101);ax.set_ylim(max(0,min(d['turns'] for d in a)-7),101)
ax.set_xlabel('Direct-heading steps that shorten the route (%)');ax.set_ylabel('Flyer-turn steps that shorten the route (%)');ax.legend(loc='lower right',frameon=False);ax.set_title('One game seed per setup')
bx.axhspan(94.96,96.34,color='#dce4e8',alpha=.8,label='Earlier two-map result')
bx.plot(range(1,len(a)+1),[d['direct'] for d in a],color='#9da5ab',lw=1,alpha=.7,label='Direct / wait if blocked')
bx.scatter(range(1,len(a)+1),[d['turns'] for d in a],c=[colors[d['source']] for d in a],s=19,label='Flyer turns',zorder=3)
bx.set_xlabel('Setups, ordered by flyer-turn progress rate');bx.set_ylabel('Steps that shorten the route (%)');bx.set_title('The spread matters');bx.set_ylim(max(0,min([d['direct'] for d in a]+[0])-4),101);bx.legend(frameon=False,loc='lower right',fontsize=8)
fig.suptitle('Direction fallback accuracy across varied matches',fontsize=14)
fig.supxlabel(f'{len(a)} setups with at least 100 pending movement requests each. Hypothetical steps; units were not moved.',fontsize=9)
fig.savefig(BASE/'accuracy-spread.png',dpi=160);fig.savefig(BASE/'accuracy-spread.pdf');print('Plotted',len(a),'cases')
