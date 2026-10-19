from pathlib import Path
import json,csv
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
import numpy as np
B=Path(__file__).resolve().parent;d=json.loads((B/'RESULTS.json').read_text());short=json.loads((B/'DEV-SHORTLIST.json').read_text())['shortlist_units'];thresholds=['1','2','4','8','16','32','64','96','128','192','256','never']
with (B/'thresholds.csv').open('w') as f:
 keys=['population','threshold_units','net_direction_benefit','harm_selected','positive_captured_fraction','selection_percent','actual_benefit_of_selected','fixed128_cost_regret'];w=csv.writer(f);w.writerow(keys)
 for pop in ['development','holdout']:
  for t in thresholds:
   v=d[pop]['thresholds'][t];w.writerow([pop,t,v['weighted_net_direction_benefit'],v['weighted_harm_selected'],v['positive_benefit_captured_fraction'],v['weighted_selection_pct'],v['selected_actual_mean'],v['fixed128_cost_regret']])
plt.rcParams.update({'font.size':10,'axes.spines.top':False,'axes.spines.right':False,'savefig.dpi':180})
fig,axes=plt.subplots(1,2,figsize=(11,4.6),sharey=True)
for ax,pop,title,color in zip(axes,['development','holdout'],['DEV (in-sample; shortlist only)','Heldout (descriptive; no selection)'],['#0072B2','#009E73']):
 vals=d[pop]['thresholds'];ax.plot([vals[t]['weighted_selection_pct'] for t in thresholds],[vals[t]['weighted_net_direction_benefit'] for t in thresholds],'o-',color=color,ms=4,lw=1)
 for t in ['1','4','32','64','128']:
  v=vals[t];x=v['weighted_selection_pct'];y=v['weighted_net_direction_benefit'];ax.annotate(t,(x,y),xytext=(5,6),textcoords='offset points',fontsize=9)
  if pop=='holdout' and int(t) in short:
   lo,hi=d['paired_map_uncertainty'][t+'_minus_never']['percentile95'];ax.errorbar(x,y,yerr=[[y-lo],[hi-y]],color=color,capsize=3)
 ax.scatter(0,0,color='black',marker='x',zorder=4);ax.annotate('never',(0,0),xytext=(3,-17),textcoords='offset points',fontsize=9)
 ax.set_xscale('symlog',linthresh=.05);ax.set_xlim(-.01,55);ax.set_xticks([0,.1,1,10,40]);ax.set_xticklabels(['0','0.1','1','10','40']);ax.axhline(0,color='#888888',lw=.7);ax.grid(alpha=.16);ax.set_title(title);ax.set_xlabel('Weighted selected episodes (%) — proxy, not CPU')
axes[0].set_ylabel('Net actual direction benefit / weighted episode')
fig.suptitle('Fixed gen003 model: threshold tradeoff',fontsize=14);fig.text(.5,.01,'H128 labels; same rows/weights. Labels identify thresholds in direction-cost units. Heldout bars: paired whole-map 95% intervals.',ha='center',fontsize=8);fig.tight_layout(rect=[0,.04,1,.94])
for ext in ['png','pdf']:fig.savefig(B/('threshold-frontier.'+ext))
plt.close(fig)
fig,axes=plt.subplots(1,2,figsize=(12,4.5),gridspec_kw={'width_ratios':[3,1]})
sets=[(['stalls','invalid_goals','missed_moves','escape_failures','false_unreachable','false_reachable'],['Stalls','Invalid goals','Missed moves','Escape failure','False unreachable','False reachable'],'Movement'),(['planning_false_negative','planning_false_positive'],['False unreachable','False reachable'],'Planning\n(eligible episodes only)')]
for ax,(keys,labels,title) in zip(axes,sets):
 x=np.arange(len(keys));width=.25
 for j,(t,c) in enumerate(zip(['1','4','128'],['#0072B2','#E69F00','#009E73'])):
  vals=[1000*d['holdout']['thresholds'][t]['outcomes'][k]['event_delta_per_query'] for k in keys];ax.bar(x+(j-1)*width,vals,width,label=t,color=c)
 ax.set_xticks(x);ax.set_xticklabels(labels,rotation=35,ha='right');ax.axhline(0,color='black',lw=.7);ax.set_title(title,fontsize=10);ax.grid(axis='y',alpha=.15)
axes[0].set_ylabel('Event change per 1,000 queries vs never');axes[0].legend(title='Threshold');fig.suptitle('Heldout outcome changes (separate from direction-cost benefit)',fontsize=13);fig.text(.5,.01,'Negative = fewer events. Offline overlapping counterfactual episodes, not whole-game outcomes; no CPU conversion.',ha='center',fontsize=8);fig.tight_layout(rect=[0,.03,1,.94])
for ext in ['png','pdf']:fig.savefig(B/('outcome-changes.'+ext))
