from pathlib import Path
import json
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
import numpy as np
p=Path(__file__).parent;d=json.loads((p/'no-cap-analysis.json').read_text());r={x['key']:x for x in d['runs']};a=r['8v8-warm4096-age-quality-capped'];b=r['8v8-warm4096-age-quality-uncapped'];fig,axs=plt.subplots(1,2,figsize=(11,4.5),layout='constrained')
x=np.arange(3);w=.36
for i,(label,row,color) in enumerate([('128-tick cap',a,'#25827b'),('No cap',b,'#bd4a35')]):
 values=[row['ages']['movement']['all'][q] for q in ['p50','p90','p99']];bars=axs[0].bar(x+(i-.5)*w,values,w,label=label,color=color);axs[0].bar_label(bars,padding=3,fontsize=9)
axs[0].set_xticks(x,['Median','90th percentile','99th percentile']);axs[0].set_yscale('log');axs[0].set_ylim(40,12000);axs[0].set_ylabel('Serving capture age (ticks, logarithmic scale)');axs[0].set_title('Ages encountered by movement queries');axs[0].legend(loc='upper left',fontsize=9);axs[0].grid(axis='y',alpha=.2);axs[0].set_axisbelow(True)
values=[100*row['movement_quality']['mean']['flagged'] for row in [a,b]];bars=axs[1].bar(['128-tick cap','No cap'],values,color=['#25827b','#bd4a35'],width=.5);axs[1].bar_label(bars,fmt='%.1f%%',padding=4);axs[1].set_ylim(0,43);axs[1].set_ylabel('Flagged sampled decisions (%)');axs[1].set_title('Fresh-reference movement quality');axs[1].grid(axis='y',alpha=.2);axs[1].set_axisbelow(True)
fig.suptitle('One late 8v8 starting state: cap removal saves 3.4% CPU/tick',fontsize=12)
fig.savefig(p/'no-cap-aging.png',dpi=160);fig.savefig(p/'no-cap-aging.svg')
