#!/usr/bin/env python3
"""Publish seed-cluster statistics and figures for the local FourSquares audit."""
import json
from pathlib import Path
from collections import Counter,defaultdict
from statistics import mean,median
import sys
import numpy as np
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt

out=Path(sys.argv[1]).resolve();s=json.loads((out/'analysis.json').read_text())
rows=[json.loads(p.read_text()) for p in sorted((out/'analysis-cache').glob('match-*.json'))]
blocks=defaultdict(list)
for r in rows:blocks[r['block']].append(r)
blocks={b:sorted(rs,key=lambda r:r['seat']) for b,rs in blocks.items() if len(rs)==4}
rows=[r for rs in blocks.values() for r in rs];n=len(rows);nb=len(blocks);rng=np.random.default_rng(532)
indices=rng.integers(0,nb,size=(15000,nb))
def boot(values):
 a=np.asarray(values,dtype=float);draw=a[indices].mean(axis=1)
 return [float(x) for x in np.quantile(draw,[.025,.975])]
def p(v):return f'{100*v:.1f}%'
def f(v):return '—' if v is None else f'{v:,.0f}'
winmatrix=np.array([[r['outcome']=='win' for r in rs] for rs in blocks.values()],dtype=int)
observed=winmatrix.sum(axis=0);teststat=((observed-observed.mean())**2).sum()
permutations=np.argsort(rng.random((15000,nb,4)),axis=2)
sim=np.take_along_axis(np.broadcast_to(winmatrix,(15000,nb,4)),permutations,axis=2).sum(axis=1)
seat_p=float((np.sum(((sim-sim.mean(axis=1,keepdims=True))**2).sum(axis=1)>=teststat)+1)/15001)
checkpoint_stats=[]
for tick in (5000,10000,15000,20000,25000,30000):
 byblock=[]
 for rs in blocks.values():
  cp=[next((c for c in r['checkpoints'] if c['tick']==tick),None) for r in rs]
  if any(c is None for c in cp):continue
  byblock.append(cp)
 if len(byblock)!=nb:continue
 x={'tick':tick,'games':n}
 for k in ('population','workers','combat_ready_warriors','attack_power','swarm_working'):
  own=[mean(c['maxima_'+k] for c in cp) for cp in byblock];enemy=[mean(c['nicowar_'+k] for c in cp) for cp in byblock]
  x[k]={'maxima':mean(own),'nicowar':mean(enemy),'maxima95':boot(own),'nicowar95':boot(enemy),'difference':mean(np.array(own)-enemy),'difference95':boot(np.array(own)-enemy)}
 checkpoint_stats.append(x)
launchrows=[r for r in rows if r['launched_flags']];no_launch=[r for r in rows if not r['launched_flags']]
invasion=[r['first_invasion'] for r in rows if r['first_invasion'] is not None]
extra={'seat_permutation_p':seat_p,'checkpoint_seed_cluster_comparisons':checkpoint_stats,'no_launch_games':len(no_launch),'no_launch_outcomes':dict(Counter(r['outcome'] for r in no_launch)),'launch_outcomes':dict(Counter(r['outcome'] for r in launchrows)),'median_first_invasion':median(invasion) if invasion else None,'invasion_games':len(invasion)}
(out/'statistical-evidence.json').write_text(json.dumps(extra,indent=2)+'\n')
plt.rcParams.update({'font.family':'DejaVu Sans','font.size':11,'axes.spines.top':False,'axes.spines.right':False,'figure.facecolor':'white','axes.titleweight':'bold'})
blue='#2364aa';gray='#777d86';orange='#d98324'
fig,axes=plt.subplots(1,2,figsize=(12,4.5),layout='constrained')
for ax,key,title,ylabel in [(axes[0],'population','Population growth','Units per team'),(axes[1],'combat_ready_warriors','Combat-trained warriors','Warriors with both combat upgrades')]:
 for role,color,label in [('maxima',blue,'Maxima'),('nicowar',gray,'Mean of three Nicowars')]:
  x=[v['tick']/1000 for v in checkpoint_stats];y=[v[key][role] for v in checkpoint_stats];lo=[v[key][role+'95'][0] for v in checkpoint_stats];hi=[v[key][role+'95'][1] for v in checkpoint_stats]
  ax.plot(x,y,'o-',color=color,label=label);ax.fill_between(x,lo,hi,color=color,alpha=.13)
 ax.set(title=title,xlabel='Game ticks (thousands)',ylabel=ylabel);ax.grid(axis='y',alpha=.2);ax.legend(frameon=False,fontsize=9)
fig.suptitle(f'FourSquares FFA4 · {n} games / {nb} seed blocks\nShading: 95% bootstrap intervals clustered by seed',fontsize=13)
fig.savefig(out/'development.png',dpi=170);plt.close(fig)
fig,ax=plt.subplots(figsize=(8,4.6),layout='constrained')
for i,r in enumerate(s['seats']):
 rate=r['win_rate'];lo,hi=r['wilson95'];ax.bar(i,100*rate,color=blue,width=.55);ax.errorbar(i,100*rate,yerr=[[max(0,100*(rate-lo))],[max(0,100*(hi-rate))]],fmt='none',color='#222',capsize=5)
 ax.text(i,100*hi+1,f"{r['wins']}/{r['games']}",ha='center')
ax.axhline(25,color=gray,ls='--',lw=1,label='25% equal-player reference');ax.set_xticks(range(4),['Seat 0','Seat 1','Seat 2','Seat 3']);ax.set(ylabel='Maxima win rate (%)',title='Wins by starting seat',ylim=(0,max(55,max(r['wilson95'][1]*100+8 for r in s['seats']))));ax.legend(frameon=False);ax.grid(axis='y',alpha=.15)
fig.savefig(out/'seats.png',dpi=170);plt.close(fig)
gates=Counter(s['gate_ticks']);labels={'blocked: too few free warriors':'Below trained-force requirement','colony emergency':'Colony emergency','open, but no reachable beatable target':'No reachable / beatable target','open':'Authorized; waiting for execution/rally','cooldown':'Cooldown','muster':'Mustering','transit':'Advancing','engage':'Engagement phase','withdraw':'Withdrawing'}
ordered=sorted(gates.items(),key=lambda x:x[1],reverse=True);den=sum(gates.values());fig,ax=plt.subplots(figsize=(10,5.4),layout='constrained')
ax.barh([labels.get(k,k) for k,v in ordered],[100*v/den for k,v in ordered],color=[blue if k in ('transit','engage') else gray for k,v in ordered]);ax.invert_yaxis();ax.set(xlabel='Share of Maxima lifetime (%)',title='Siege gate and warrior mission activity');ax.grid(axis='x',alpha=.15)
for i,(k,v) in enumerate(ordered):ax.text(100*v/den+.4,i,f'{100*v/den:.1f}%',va='center',fontsize=9)
ax.set_xlim(0,max(v for k,v in ordered)*100/den+9);fig.savefig(out/'gates.png',dpi=170);plt.close(fig)
ci=s['win_rate_cluster95'];counts=s['outcomes'];tot=s['totals'];active=s['phase_ticks'].get('transit',0)+s['phase_ticks'].get('engage',0);sup=s['superiority'];opp=s['opportunity_ticks'];oppden=sum(opp.values())
text=f'''# FourSquares FFA4: Maxima versus Nicowar

{'FINAL' if n==s['expected_games'] else 'PRELIMINARY'} — {n} analyzed games, {nb} independent seed blocks. One Maxima against three unallied Original Nicowars. Each seed is tested with Maxima in all four seats. The local-only batch used six workers, a frozen binary/data snapshot and a 180,000-game-tick horizon. No AI policy was changed during the tournament.

## Outcomes

- Maxima: **{counts.get('win',0)} wins, {counts.get('loss',0)} losses, {counts.get('unresolved',0)} unresolved**.
- Observed win rate: **{p(s['win_rate'])}**, 95% seed-cluster interval **{p(ci[0])}–{p(ci[1])}**.
- Median Maxima elimination/end-of-observation: **{f(s['median_life_end'])} game ticks**.
- Games with a warrior offensive launch: **{s['games_with_launch']}/{n}**. Median first launch among these games: **{f(s['median_first_launch'])} ticks**. This conditional median is not an estimate for games that never launched.
- Games ever reaching 11 trained warriors beyond the defense reserve: **{s['games_with_trained_surplus']}/{n}**. This is a necessary count threshold, not proof of health or route eligibility.

| Maxima seat | Wins | Games | Win rate | 95% Wilson interval |
| --- | ---: | ---: | ---: | --- |
'''
for r in s['seats']:text+=f"| {r['seat']} | {r['wins']} | {r['games']} | {p(r['win_rate'])} | {p(r['wilson95'][0])}–{p(r['wilson95'][1])} |\n"
text+=f'''\nSeat differences: seed-stratified permutation test p={seat_p:.4f}; the test shuffles the four outcomes within each seed and uses dispersion of seat win totals. This tests seat association, not its cause. The 25% reference is the equal-player benchmark, not a promise that different AIs should be equal.

![Starting-seat outcomes](seats.png)

## Development and army strength

The Nicowar comparison below is the mean of the three opponents within each match, then averaged across games. Eliminated teams remain in the observer comparisons; their recorded populations are retained. Comparisons use only checkpoints present in every complete seed block.

| Game tick | Maxima population | Nicowar population | Maxima trained warriors | Nicowar trained warriors |
| --- | ---: | ---: | ---: | ---: |
'''
for x in checkpoint_stats:text+=f"| {x['tick']:,} | {x['population']['maxima']:.1f} | {x['population']['nicowar']:.1f} | {x['combat_ready_warriors']['maxima']:.1f} | {x['combat_ready_warriors']['nicowar']:.1f} |\n"
text+='\n![Development trajectories](development.png)\n\nPaired, seed-cluster confidence intervals for Maxima minus mean Nicowar:\n\n'
for x in checkpoint_stats:
 if x['tick'] in (15000,20000):
  for k,label in [('population','population'),('combat_ready_warriors','trained warriors'),('attack_power','team attack power')]:
   v=x[k];text+=f"- Tick {x['tick']:,}, {label}: {v['difference']:+.1f}, 95% interval [{v['difference95'][0]:+.1f}, {v['difference95'][1]:+.1f}].\n"
text+=f'''\n## Tactical activity

Across Maxima's observed lifetime, warrior offensive missions were advancing or in their engagement phase for **{p(active/tot['life_end'])}** of the time. The remainder includes the opening, defense, waiting, mustering and withdrawing. Phase state does not measure individual weapon swings. Actual warrior attack animations occurred during {p(tot['any_fighting_ticks']/tot['alive_observer_ticks'])} of sampled lifetime, including defensive fights.

![Tactical gates](gates.png)

When there were at least 11 trained warriors beyond reserve, **{p((opp.get('transit',0)+opp.get('engage',0))/oppden) if oppden else '—'}** of that time was advancing/engaged. The largest other gate in this subset was: {max(((k,v) for k,v in opp.items() if k not in ('transit','engage')),key=lambda x:x[1],default=('none',0))[0]}. This gate currently combines path feasibility, available eligible units and target strength; telemetry does not isolate those causes individually.

There were **{tot['missions']} mission starts and {tot['launched_flags']} launched flags**, with **{s['mission_finishes'].get('muster_underfilled',0)} muster timeouts**. Requested force counts: {s['requests']}.

Withdrawal reasons: {s['withdraw_reasons']}. Other mission finish reasons: {s['mission_finishes']}. `no_safe_rally` counts retries before a mission exists, so those retries are not independent failed attacks.

## Stronger-army windows

Define an advantage window as at least 11 fully combat-trained warriors and team attack power at least 1.5× the strongest living opponent. This uses omniscient observer measurements only for analysis; it was not supplied to Maxima. It is not superiority over all three opponents combined and does not establish health or swimming eligibility.

**{s['games_with_superiority']}/{n}** games had at least 1,000 ticks in such a window; **{s['games_with_superiority_and_no_launch']}** of those never launched a warrior offensive mission. Across all advantage-window time, **{p(sup.get('inactive',0)/sup['total']) if sup.get('total') else '—'}** was outside advancing/engagement phases. This separates army underuse from the many games where Maxima was already materially weaker. It includes late cleanup against nearly eliminated opponents, and longer windows carry more weight.

## Concrete examples and additional activity

- Match 28 (seed 1666030424, Maxima seat 3) reached the 180,000-tick limit with **80 Maxima units versus two remaining Nicowar units**. Maxima had **21 fully trained, swimming warriors** and a reserve of 17. The late director snapshots repeatedly report `blocked: too few free warriors`. At the end, no warriors were assigned to war flags. Route-clearing operations had occurred earlier. This is a concrete cleanup failure caused at least at the authorization stage by the fixed force threshold.
- Match 26 (the same seed, Maxima seat 1) won with **zero warrior offensive launches**. It spent 24,530 ticks in the stronger-army condition. Late snapshots show 70–71 trained warriors, a reserve of 23, and `opening route to sealed team 3`, while the siege gate reports no reachable beatable target. The logs record a dig-out start. Its inactivity cannot be explained by the reserve count alone: route opening and eligible-target selection need investigation.
- Explorer attacks are separate from the warrior mission state: the logs contain **38 explorer-strike launches across 12 games**. There were also **18 dig-out starts across 11 games**. The 2.3% warrior mission activity statistic excludes these actions and defensive combat.

## Interpretation and next experiments

The current strategy holds a defense reserve floor of 17 trained warriors and requests at least 11 for a siege. Consequently it generally needs 28 trained warriors before even authorizing a siege, despite the launch quorum now being 50% of the requested force. A smaller quorum cannot help games that never reach mission authorization. The below-force gate accounts for **72.5%** of recorded lifetime; the combined no-reachable/beatable-target gate accounts for **58.6%** of time with at least 11 trained warriors beyond reserve. These are observed gate shares, not estimates of the benefit of changing them.

The development comparisons and gate durations distinguish early force/economic weakness from later execution problems. They do not prove that lowering reserves or increasing birth rates improves results. The next controlled experiments should isolate (1) a threat-sensitive reserve and endgame cleanup threshold, (2) early economic and training throughput, and (3) offensive force sizing once a surplus exists. Before tuning route behavior, split the combined rejection telemetry into missing routes, unavailable eligible warriors, excessive required power, and target exclusions. The strategy requested 11 warriors in 50 of 56 mission starts; large armies are usually committed in small packets. Use the same seed/seat blocks for paired comparisons, followed by fresh held-out seeds; do not change several policies at once and attribute gains to one.

The fixed dead-opponent target lock is included in this binary. This batch is not a before/after experiment and cannot estimate the causal benefit of that fix or the 50% quorum.

## Methods and reproducibility

- Seed-cluster bootstrap: 15,000 draws for checkpoint comparisons, 12,000 draws for win-rate interval endpoints; four rotations from a seed remain together. Seat intervals have one game per independent seed.
- If a win-rate sample is entirely zero or one, use a conservative exact seed-level bound rather than a degenerate bootstrap interval.
- Unresolved matches at the simulation horizon remain unresolved. Win rate means a recorded win by that horizon; unresolved games are not relabeled losses or awarded fractional wins.
- Observer snapshots are sampled every 100 game ticks. Gate durations carry each director snapshot forward to the next, bounded by Maxima's observed elimination. Mission-phase durations use explicit game-tick event timestamps. AI ticks and game ticks are not interchangeable.
- Time shares pool unit/tick exposure; longer-lived games contribute more. Win rates and checkpoint comparisons weight complete seed blocks equally.
- `combat_ready_warriors` in engine telemetry means both combat upgrades; it does not check current medical state. `warriors_flagged` includes defensive assignments. `casualties` withdrawals compare flag enrollment, so feeding/healing departures cannot be distinguished from actual losses with these logs.
- Raw compressed logs: `logs/`; structured decision and observer telemetry: `match-telemetry/`; frozen executable, data and source: `snapshot/`; checksums and complete schedule: `manifest.json`; match outcomes: `results.json`; per-game features: `game-features.csv`; detailed statistics: `analysis.json` and `statistical-evidence.json`.
'''
(out/'report.md').write_text(text)
print('Wrote',out/'report.md')
