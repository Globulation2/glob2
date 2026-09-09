#!/usr/bin/env python3
"""Compare complete paired seed blocks from two frozen FourSquares tournaments."""
import argparse
from collections import Counter,defaultdict
import json
from pathlib import Path
from statistics import mean,median
import numpy as np
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt

p=argparse.ArgumentParser(description=__doc__);p.add_argument('baseline',type=Path);p.add_argument('variant',type=Path);a=p.parse_args()
base=a.baseline.resolve();out=a.variant.resolve()
def read(folder):return {r['id']:r for file in (folder/'analysis-cache').glob('match-*.json') for r in [json.loads(file.read_text())]}
b=read(base);v=read(out);blocks=defaultdict(list)
for i in sorted(b.keys()&v.keys()):
 assert (b[i]['seed'],b[i]['seat'])==(v[i]['seed'],v[i]['seat'])
 blocks[b[i]['block']].append((b[i],v[i]))
blocks={k:rs for k,rs in blocks.items() if len(rs)==4};pairs=[pair for rs in blocks.values() for pair in rs];n=len(pairs);nb=len(blocks)
if not n:raise SystemExit('No complete paired seed blocks yet')
rng=np.random.default_rng(20260905);indices=rng.integers(nb,size=(24000,nb))
def comparison(fn):
 bv=np.array([mean(fn(x) for x,y in rs) for rs in blocks.values()]);vv=np.array([mean(fn(y) for x,y in rs) for rs in blocks.values()]);d=vv-bv
 return {'baseline':float(bv.mean()),'variant':float(vv.mean()),'difference':float(d.mean()),'paired_seed_cluster95':[float(x) for x in np.quantile(d[indices].mean(axis=1),[.025,.975])]}
metrics={'win':lambda r:r['outcome']=='win','launch':lambda r:r['launched_flags']>0,'survive_30000':lambda r:r['life_end']>=30000,'missions_per_game':lambda r:r['missions'],'trained_surplus':lambda r:r['trained_surplus_ticks']>0}
for t in (20000,30000,40000,60000):metrics[f'launch_by_{t}']=lambda r,t=t:r['first_launch'] is not None and r['first_launch']<=t
stats={k:comparison(fn) for k,fn in metrics.items()}
summary={'paired_games':n,'complete_seed_blocks':nb,'expected_games':json.loads((out/'manifest.json').read_text())['matches'],'metrics':stats,'outcome_transitions':dict(Counter(x['outcome']+' -> '+y['outcome'] for x,y in pairs)),'new_wins':[x['id'] for x,y in pairs if x['outcome']!='win' and y['outcome']=='win'],'lost_wins':[x['id'] for x,y in pairs if x['outcome']=='win' and y['outcome']!='win'],'arms':{}}
for label,k in [('baseline',0),('variant',1)]:
 rows=[pair[k] for pair in pairs];gates=Counter();phase=Counter();finishes=Counter();superiority=Counter();withdraw=Counter();opportunity=Counter()
 for r in rows:
  gates.update(r['gate_ticks']);phase.update(r['phase_ticks']);finishes.update(r['mission_finishes']);superiority.update(r['superiority']);withdraw.update(r['withdraw_reasons']);opportunity.update(r['opportunity_ticks'])
 summary['arms'][label]={'outcomes':dict(Counter(r['outcome'] for r in rows)),'games_with_launch':sum(r['launched_flags']>0 for r in rows),'median_first_launch_conditional':median([r['first_launch'] for r in rows if r['first_launch'] is not None]) if any(r['first_launch'] is not None for r in rows) else None,'median_life_end':median(r['life_end'] for r in rows),'life_ticks':sum(r['life_end'] for r in rows),'missions':sum(r['missions'] for r in rows),'launched_flags':sum(r['launched_flags'] for r in rows),'gate_ticks':dict(gates),'phase_ticks':dict(phase),'mission_finishes':dict(finishes),'withdraw_reasons':dict(withdraw),'superiority':dict(superiority),'opportunity_ticks':dict(opportunity)}
summary['seats']=[{'seat':seat,'games':sum(x['seat']==seat for x,y in pairs),'baseline_wins':sum(x['seat']==seat and x['outcome']=='win' for x,y in pairs),'variant_wins':sum(y['seat']==seat and y['outcome']=='win' for x,y in pairs)} for seat in range(4)]
(out/'paired-comparison.json').write_text(json.dumps(summary,indent=2)+'\n')
plt.rcParams.update({'font.family':'DejaVu Sans','font.size':11,'axes.spines.top':False,'axes.spines.right':False})
fig,axes=plt.subplots(1,2,figsize=(12,4.6),layout='constrained');colors=['#777d86','#2364aa'];labels=['Reserve floor 17','Reserve floor 6']
for k,(color,label) in enumerate(zip(colors,labels)):
 ticks=np.arange(0,180001,1000);rates=[100*mean(int(pair[k]['first_launch'] is not None and pair[k]['first_launch']<=t) for pair in pairs) for t in ticks]
 axes[0].plot(ticks/1000,rates,color=color,label=label,lw=2)
axes[0].set(xlabel='Game ticks (thousands)',ylabel='Games with a warrior launch (%)',title='Cumulative first offensive launches',ylim=(0,100));axes[0].legend(frameon=False);axes[0].grid(axis='y',alpha=.15)
ks=['win','launch_by_30000','survive_30000'];names=['Win by horizon','Launch by 30k','Survive to 30k']
for i,k in enumerate(ks):
 d=stats[k]['difference']*100;lo,hi=np.array(stats[k]['paired_seed_cluster95'])*100
 axes[1].errorbar(d,i,xerr=[[max(0,d-lo)],[max(0,hi-d)]],fmt='o',color=colors[1],capsize=5)
axes[1].set_yticks(range(len(ks)),names);axes[1].invert_yaxis();axes[1].axvline(0,color='#999',ls='--');axes[1].set(xlabel='Change from baseline (percentage points)',title='Paired effects with 95% seed-cluster intervals');axes[1].grid(axis='x',alpha=.15)
fig.suptitle(f'Defense reserve floor 17 → 6 · {n} paired games / {nb} seeds',fontsize=14);fig.savefig(out/'paired-comparison.png',dpi=170);plt.close(fig)
def pct(x):return f'{x*100:.1f}%'
def delta(x):return f'{x*100:+.1f} pp'
text=f'''# Defense reserve floor: paired FourSquares experiment

{'FINAL' if n==summary['expected_games'] else 'PRELIMINARY'}: {n} paired games across {nb} complete seed blocks. One Maxima versus three unallied Original Nicowars in FFA4. The same frozen executable, data, map, 180,000-tick horizon, seeds and seat rotations were used. The only resolved parameter change was `military.defense_reserve_floor = 17 → 6`. Siege minimum remains 11; muster quorum remains 50%; strength requirement remains 125%; attack cap remains 20. The repository's default strategy was not changed.

| Outcome | Floor 17 | Floor 6 | Change | 95% paired interval |
| --- | ---: | ---: | ---: | --- |
'''
for key,label in [('win','Win by horizon'),('launch','Ever launch a warrior offensive'),('launch_by_20000','Launch by tick 20,000'),('launch_by_30000','Launch by tick 30,000'),('survive_30000','Survive to tick 30,000'),('trained_surplus','Ever reach 11 trained warriors beyond reserve')]:
 r=stats[key];lo,hi=r['paired_seed_cluster95'];text+=f"| {label} | {pct(r['baseline'])} | {pct(r['variant'])} | {delta(r['difference'])} | {delta(lo)} to {delta(hi)} |\n"
text+='\n![Paired comparison](paired-comparison.png)\n\n## Match outcomes and tactical activity\n\n'
for label,name in [('baseline','Floor 17'),('variant','Floor 6')]:
 r=summary['arms'][label];active=r['phase_ticks'].get('transit',0)+r['phase_ticks'].get('engage',0)
 text+=f"- **{name}:** {r['outcomes']}; {r['missions']} mission starts, {r['launched_flags']} launched flags, {r['mission_finishes'].get('muster_underfilled',0)} muster timeouts. Advancing/engagement: {pct(active/r['life_ticks'])} of observed lifetime. Median first launch **among games that launch**: {r['median_first_launch_conditional']} ticks; median elimination/end of observation: {r['median_life_end']} ticks.\n"
text+=f"\nNew wins compared with baseline: {summary['new_wins']}. Baseline wins no longer won: {summary['lost_wins']}.\n\nMatched outcome transitions: {summary['outcome_transitions']}.\n\n"
text+='## Wins by starting seat\n\n| Seat | Games per arm | Floor 17 wins | Floor 6 wins |\n| --- | ---: | ---: | ---: |\n'
for r in summary['seats']:text+=f"| {r['seat']} | {r['games']} | {r['baseline_wins']} | {r['variant_wins']} |\n"
text+='\n## Siege gates\n\nShares of total recorded director-snapshot time while Maxima remained alive. Includes opening time; longer games contribute more.\n\n| Gate | Floor 17 | Floor 6 |\n| --- | ---: | ---: |\n'
allgates=set(summary['arms']['baseline']['gate_ticks'])|set(summary['arms']['variant']['gate_ticks'])
for g in sorted(allgates,key=lambda g:summary['arms']['baseline']['gate_ticks'].get(g,0),reverse=True):
 vals=[summary['arms'][k]['gate_ticks'].get(g,0)/sum(summary['arms'][k]['gate_ticks'].values()) for k in ('baseline','variant')];text+=f'| {g} | {pct(vals[0])} | {pct(vals[1])} |\n'
text+='''
## Interpretation limits

Confidence intervals resample 24,000 sets of complete seed blocks, preserving all four seats and both parameter variants together. A win-rate interval crossing zero does not establish improvement or harm. The primary outcome is recorded victory by the horizon; unresolved games remain unresolved and are not converted into losses or fractional wins. Secondary launch and survival outcomes diagnose mechanisms and are exploratory.

Cumulative launches use all paired games as the denominator, including games eliminated without launching. Conditional first-launch medians compare different subsets of games and should not be interpreted as a causal timing effect. Warrior offensive phases exclude explorer strikes and defensive fighting. Reserve floors interact with enemy-based and army-size-based reserve terms; lowering the floor does not always lower the actual reserve.

This experiment isolates the parameter change in this map/lineup and these sampled seeds. Choosing a policy from these results should be followed by validation on fresh seeds. It does not isolate downstream changes in economy or troop health caused by altered policy decisions.

Raw compressed logs and decision/observer telemetry are retained in `logs/` and `match-telemetry/`. Exact paired statistics are in `paired-comparison.json`; per-game features are in `game-features.csv`; the frozen inputs and checksums are recorded in `snapshot/` and `manifest.json`.
'''
validation_file=out/'validation.json'
if validation_file.exists():
 check=json.loads(validation_file.read_text())
 if check.get('crashed_first_attempts'):
  text+=f"\n## Run validation\n\nAll {check['completed_matches']} scheduled matches have complete results and frozen-input checksums were verified. One first attempt (match {check['retry_id']}) crashed after Maxima had already been eliminated at tick {check['candidate_elimination_tick']:,}. Its normal retry completed successfully; Maxima's observer history through elimination was identical. The original attempt and crash artifacts are retained in `attempts/` and `crashes/`.\n"
(out/'comparison-report.md').write_text(text)
print(json.dumps({k:v for k,v in summary.items() if k!='arms'},indent=2))
