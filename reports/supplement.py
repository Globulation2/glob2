"""Paired ancillary diagnostics and plot after the frozen cohort is complete."""
import json,random,statistics,sys,shutil
from pathlib import Path
from collections import defaultdict,Counter
R=Path(__file__).resolve().parent;sys.path.insert(0,str(R.parents[2]))
from tools.tournaments.analysis import _block_mean_interval
A=json.loads((R/'assessment.json').read_text());assert A['games']==288 and A['complete_team_histories_integrals_exact']==960
T={t['job']:t['subject'] for t in A['telemetry']};C=(('adaptive','baseline'),('single','baseline'),('adaptive','single'))
METRICS={'schools':1,'trained_workers':1,'technology_workers':1,'population':1,'workers':1,'building_sites':1,'unserved_food_active_fraction':100,'critical_food_active_fraction':100}
def ci(rows,values):
 blocks=defaultdict(list)
 for row,value in zip(rows,values):blocks[row['block']].append(value)
 assert len(blocks)==16
 return {'mean':statistics.mean(values),'interval95':_block_mean_interval(blocks,10000,random.Random(19)),'pairs':len(rows),'seed_blocks':16}
D={'scope':'Ancillary paired diagnostics; all games retained, bootstrap 10000 draws seed19 by whole seed block. Fractions are sampled director-state hold estimates, not measured output or causal spending.','bundle':A['bundle'],'manifest_sha256':A['manifest_sha256'],'formats':{}}
for fmt in ('1v1','ffa'):
 rows=[p for p in A['pairs_detail'] if p['format']==fmt];out={'contrasts':{},'coverage':{},'capacity_exposure':{},'largest_adaptive_vs_single_changes':{}}
 for a,b in C:
  e={}
  for key,scale in METRICS.items():
   assert all(T[p[x]['job']]['economic_sample_hold_means'] is not None for p in rows for x in (a,b))
   e[key]=ci(rows,[(T[p[a]['job']]['economic_sample_hold_means'][key]-T[p[b]['job']]['economic_sample_hold_means'][key])*scale for p in rows])
  for key in ('sole_engine_win','shared_engine_win','sole_natural_prestige_win','shared_natural_prestige_win'):
   e[key+'_percentage_points']=ci(rows,[(int(p[a][key])-int(p[b][key]))*100 for p in rows])
  e['any_engine_win_percentage_points']=ci(rows,[(int(0 in p[a]['engine_winners'])-int(0 in p[b]['engine_winners']))*100 for p in rows])
  out['contrasts'][a+'_minus_'+b]=e
 for arm in ('baseline','single','adaptive'):
  s=[T[p[arm]['job']] for p in rows]
  raw=Counter();peaks=Counter()
  for t in s:
   raw.update(t['chosen_capacity_observations'])
   peaks[str(max((int(k) for k,v in t['chosen_capacity_observations'].items() if v),default=0))]+=1
  out['capacity_exposure'][arm]={'helper_chosen_observations':dict(raw),'games_by_max_helper_choice':dict(peaks),'sparse_director_game_maxima':A['by_format'][fmt]['arms'][arm]['games_by_maximum_inferred_capacity']}
  out['coverage'][arm]={'games':len(s),'missing_games':sum(t['economic_sample_hold_means'] is None for t in s),'director_start_gap_ticks_max':max(t['director_start_gap_ticks'] for t in s),'director_tail_hold_ticks_max':max(t['director_tail_gap_ticks'] for t in s),'economic_coverage_ticks_total':sum(t['economic_coverage_ticks'] for t in s)}
 ordered=sorted(rows,key=lambda p:p['adaptive_minus_single'])
 for label,selected in (('declines',ordered[:3]),('gains',ordered[-3:][::-1])):
  out['largest_adaptive_vs_single_changes'][label]=[{'pair':p['pair'],'change_pp':100*p['adaptive_minus_single'],'single':{k:p['single'][k] for k in ('engine_outcome','adjudicated_outcome','ticks','sole_natural_prestige_win','shared_natural_prestige_win')},'adaptive':{k:p['adaptive'][k] for k in ('engine_outcome','adjudicated_outcome','ticks','sole_natural_prestige_win','shared_natural_prestige_win')}} for p in selected]
 out['adaptive_vs_single_pair_signs']={name:sum((p['adaptive_minus_single']>0 if name=='improve' else p['adaptive_minus_single']<0 if name=='decline' else p['adaptive_minus_single']==0) for p in rows) for name in ('improve','decline','same')}
 D['formats'][fmt]=out
text=json.dumps(D,indent=2)+'\n';assert len(text.encode())<2*1024**2 and shutil.disk_usage(R).free>2*1024**3
(R/'supplement.json').write_text(text)
lines=['# Ancillary economic and outcome comparisons','','All paired seeds and rotations are included. Intervals use the same whole-seed bootstrap as the primary analysis; these are exploratory diagnostics, without adjustment for multiple comparisons. Food fractions mean time with a nonzero director-reported shortfall. They do not measure realized spending or causality.','','| Format | Contrast | Schools, change [95% CI] | Trained workers, change [95% CI] | Unserved food time, pp [95% CI] | Critical food time, pp [95% CI] |','| --- | --- | --- | --- | --- | --- |']
for fmt,f in D['formats'].items():
 for name,e in f['contrasts'].items():
  cell=lambda key:f"{e[key]['mean']:+.3f} [{e[key]['interval95'][0]:+.3f}, {e[key]['interval95'][1]:+.3f}]"
  lines.append('| '+ ' | '.join([fmt,name,*[cell(k) for k in ('schools','trained_workers','unserved_food_active_fraction','critical_food_active_fraction')]])+' |')
lines+=['','The helper emits at each strategic decision; director snapshots emit only every1,000AI ticks. Capacity exposure below uses every helper decision, while the JSON retains the sparser director-derived series separately. Chosen allowance is not a claim of simultaneous funded spending.','','| Format/arm | Games with maximum0 /1 /2 /3 /4 |','| --- | --- |']
for fmt,f in D['formats'].items():
 for arm,c in f['capacity_exposure'].items():lines.append('| '+fmt+'/'+arm+' | '+' / '.join(str(c['games_by_max_helper_choice'].get(str(k),0)) for k in range(5))+' |')
lines+=['','Full coverage, actual-win contrasts, adverse cases and all metrics: [supplement.json](supplement.json). Primary effectiveness: [assessment.md](assessment.md).','','![Primary effects and selected capacity](overview.png)']
(R/'supplement.md').write_text('\n'.join(lines)+'\n')
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
fig,axes=plt.subplots(1,2,figsize=(12,4.8),layout='constrained')
colors={'1v1':'#1f77b4','ffa':'#d97706'};names={'adaptive_minus_baseline':'Adaptive − off','single_minus_baseline':'Single − off','adaptive_minus_single':'Adaptive − single'}
for fmt,f in A['by_format'].items():
 for i,(name,label) in enumerate(names.items()):
  e=f['contrasts'][name];v=100*e['mean'];lo,hi=[100*x for x in e['interval95']];y=i+(-.12 if fmt=='1v1' else .12)
  axes[0].errorbar(v,y,xerr=[[v-lo],[hi-v]],fmt='o',capsize=4,color=colors[fmt],label=('Duels' if fmt=='1v1' else 'Four-player FFA') if i==0 else None)
axes[0].axvline(0,color='#888',lw=1);axes[0].set_yticks(range(3),names.values());axes[0].invert_yaxis();axes[0].set_xlabel('Whole-match model score change (percentage points)');axes[0].set_title('Paired effects, 95% seed-block intervals');axes[0].legend()
for j,(fmt,f) in enumerate(A['by_format'].items()):
 c=D['formats'][fmt]['capacity_exposure']['adaptive']['games_by_max_helper_choice'];n=sum(c.values());xs=[k+(-.18 if j==0 else .18) for k in range(5)]
 axes[1].bar(xs,[100*c.get(str(k),0)/n for k in range(5)],width=.34,color=colors[fmt],label='Duels' if fmt=='1v1' else 'Four-player FFA')
axes[1].set_xticks(range(5));axes[1].set_xlabel('Largest selected pipeline allowance (0 = inactive)');axes[1].set_ylabel('Share of adaptive games (%)');axes[1].set_title('Adaptive allowance at strategic decisions');axes[1].legend()
fig.suptitle('Maxima prestige: 288 games · SIM117 · 16 seed blocks per format')
for ext in ('png','svg','pdf'):fig.savefig(R/('overview.'+ext),dpi=160)
plt.close(fig)
assert sum((R/('overview.'+ext)).stat().st_size for ext in ('png','svg','pdf'))<8*1024**2
print(json.dumps({'ancillary_report':'supplement.md','plot':'overview.png','bundle':A['bundle']}))
