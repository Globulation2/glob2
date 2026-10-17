import json
from pathlib import Path
import numpy as np
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
root=Path(__file__).parent/'runs';groups=json.loads((root/'summary.json').read_text())
plt.rcParams.update({'font.family':'DejaVu Sans','font.size':10})
fig,(ax,bx)=plt.subplots(1,2,figsize=(11,4.5),gridspec_kw={'width_ratios':[1.15,1]})
labels=[];values=[];lo=[];hi=[]
for g in groups:
 m=g['metrics']['cpu_us_tick'];labels.append(('4 teams' if g['workload']=='holiday' else '11 teams')+f", {g['participants']} participant"+('s' if g['participants']>1 else ''));values.append(m['change_pct']);lo.append(m['change_pct']-m['paired_bootstrap_95_pct'][0]);hi.append(m['paired_bootstrap_95_pct'][1]-m['change_pct'])
y=np.arange(4);ax.errorbar(values,y,xerr=[lo,hi],fmt='o',color='#176b91',capsize=4);ax.set_yticks(y,labels);ax.invert_yaxis();ax.axvline(0,color='#888',lw=1);ax.set_xlabel('CPU per tick: change (%)\nPaired bootstrap 95% interval');ax.set_title('Average CPU cost increased');ax.grid(axis='x',alpha=.2)
base=[g['metrics']['pending_pct']['baseline'] for g in groups[::2]];new=[g['metrics']['pending_pct']['proactive'] for g in groups[::2]];x=np.arange(2);bx.bar(x-.18,base,.36,label='Baseline',color='#aaa');bx.bar(x+.18,new,.36,label='Proactive',color='#176b91');bx.set_xticks(x,['4 teams, 128×128','11 teams, 256×256']);bx.set_ylabel('Pending route queries (%)\nMean per game');bx.set_title('Fewer queries found a missing field');bx.legend(frameon=False);bx.grid(axis='y',alpha=.2);bx.set_axisbelow(True)
for i,(a,b) in enumerate(zip(base,new)):bx.text(i,max(a,b)+.002,f'{100*(b/a-1):.1f}%',ha='center')
bx.set_ylim(0,max(base)*1.22)
fig.suptitle('Proactive building-gradient initialization — 32 paired seeds per map',fontsize=13);fig.text(.5,.012,'Apple M3 • release -O3 • cap 32,768 ticks • identical global refresh limit • whole-match effects included',ha='center',fontsize=9,color='#555');fig.tight_layout(rect=[0,.05,1,.94]);fig.savefig(root/'comparison.png',dpi=180)
