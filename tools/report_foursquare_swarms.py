#!/usr/bin/env python3
"""Report the saved-data swarm audit with seed-cluster comparisons."""
import json,sys
from pathlib import Path
from collections import defaultdict
from statistics import mean
import numpy as np
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
p=Path(sys.argv[1]);s=json.loads((p/'analysis.json').read_text());rows=json.loads((p/'checkpoint-rows.json').read_text());games=json.loads((p/'per-game.json').read_text());events=json.loads((p/'swarm-events.json').read_text());rng=np.random.default_rng(9517);ci={}
for t in (10000,15000,20000):
 blocks=defaultdict(list)
 for r in rows:
  if r['tick']==t:blocks[r['block']].append(r)
 indices=rng.integers(len(blocks),size=(16000,len(blocks)))
 for k in ('swarm_assigned','swarm_working','swarms','population'):
  d=np.array([mean(r['maxima_'+k]-r['nicowar_'+k] for r in rs) for rs in blocks.values()]);ci[f'{t}_{k}']={'difference':float(d.mean()),'seed_cluster95':[float(x) for x in np.quantile(d[indices].mean(axis=1),[.025,.975])]}
(p/'statistics.json').write_text(json.dumps(ci,indent=2)+'\n')
plt.rcParams.update({'font.family':'DejaVu Sans','font.size':11,'axes.spines.top':False,'axes.spines.right':False})
fig,axs=plt.subplots(1,2,figsize=(12,4.5),layout='constrained');cps=[r for r in s['checkpoints'] if r['tick']<=20000];xs=[r['tick']/1000 for r in cps]
for arm,color,label in [('maxima','#2364aa','Maxima'),('nicowar','#777d86','Mean Nicowar')]:
 axs[0].plot(xs,[r[arm+'_swarms'] for r in cps],'o-',color=color,label=label)
 axs[1].plot(xs,[r[arm+'_swarm_working'] for r in cps],'o-',color=color,label=label+' enrolled')
 axs[1].plot(xs,[r[arm+'_swarm_assigned'] for r in cps],'--',color=color,label=label+' requested',alpha=.65)
axs[0].set(title='Swarm buildings and construction sites',ylabel='Count per team',ylim=(0,5));axs[1].set(title='Swarm staffing',ylabel='Workers per team',ylim=(0,26))
for ax in axs:ax.set_xlabel('Game ticks (thousands)');ax.legend(frameon=False,fontsize=9);ax.grid(axis='y',alpha=.2)
fig.suptitle('FourSquares · 96 games with reserve floor 6',fontsize=14);fig.savefig(p/'swarm-staffing.png',dpi=170);plt.close(fig)
text='''# Swarm capacity and staffing audit

Reserve floor 6 is now retained in `data/maxima/base.strategy`; the engine's resolved FFA4 configuration was verified. No economic parameters were changed.

This audit uses all 96 games from the reserve-floor-6 experiment and their three unallied Nicowar opponents. Each of 24 seeds has four Maxima seat rotations. Opponent comparisons first average the three Nicowars within each game.

## Main findings

**The data supports underfunded production and an overambitious construction target. It does not support substantially more completed swarms as the main cause of the initial economic gap.**

| Game tick | Maxima population | Mean Nicowar population | Maxima swarms/sites | Mean Nicowar swarms/sites | Maxima requested/enrolled swarm workers | Mean Nicowar requested/enrolled |
| --- | ---: | ---: | ---: | ---: | --- | --- |
'''
for r in cps:text+=f"| {r['tick']:,} | {r['maxima_population']:.1f} | {r['nicowar_population']:.1f} | {r['maxima_swarms']:.2f} | {r['nicowar_swarms']:.2f} | {r['maxima_swarm_assigned']:.1f} / {r['maxima_swarm_working']:.1f} | {r['nicowar_swarm_assigned']:.1f} / {r['nicowar_swarm_working']:.1f} |\n"
text+='''
![Swarm counts and staffing](swarm-staffing.png)

At tick 20,000, Maxima is asking for only 8.3 swarm workers, with 6.9 enrolled. The gap from Nicowar is mainly in requested labor, not a failure to recruit the requested number. Enrollment is not proof of corn delivery: these workers may be traveling or hauling.

All Maxima teams are alive at ticks 10,000 and 15,000; 95/96 are alive at tick 20,000. Restricting the latter to living Maximas gives 8.4 requested and 7.0 enrolled workers versus 21.3 enrolled per opposing Nicowar. Elimination alone does not explain the staffing gap.

The engine's `swarms` count includes construction sites. Separate retirement telemetry reconstructs completed Maxima swarms at the latest logged review: about 3.3 completed at tick 15,000, versus a desired target of 5.0; about 3.2 completed among survivors at tick 20,000, versus a target of 5.9. These are approximate checkpoint counts because retirement reviews are less frequent than observer snapshots. A target is not proof that a construction order was issued or completed.

## Why the budgets disagree

- Swarm targets grow roughly with `population / 15`, up to 14, with a committed-economy target of at least three. Capacity/resource and policy gates also apply.
- The worker budget is colony-wide. In a committed economy it scales as 5, 6 or 8 workers per completed swarm, depending on food headroom, but is capped at **20 total**. Thus extra completed swarms eventually stop bringing additional labor.
- Survival policy can limit the whole colony to **10, 2 or zero** swarm workers. Abundance can bypass some of these limits.
- Food-service safeguards force the budget to **zero** if critical hunger or units without an inn reach 12% of population. At zero budget, swarm production ratios also become zero: this explicitly pauses births, including births that stored corn could otherwise support.
- Further adaptive staffing can halve birth labor when critical hunger exceeds 12%, except in recovery. Positive budgets are distributed by nearby corn capacity, with no minimum allocation per swarm.
- These staffing cuts do not lower the separate swarm construction target. Retirement removes persistently zero-supply swarms only when safe and enough useful swarms already exist; it does not prune merely underfunded capacity.

## How often the cuts occur

Using director snapshots from tick 5,000 to 30,000, bounded by Maxima's elimination:
'''
d=s['duration'];pre=s['pre_invasion_duration']
for key,label in [('budget_at_most_two','Budget at most two workers'),('budget_zero','Budget zero / births explicitly paused'),('demand_more_with_budget_zero','Desired swarm count exceeds existing count while budget is zero'),('budget_twenty','Budget reaches the 20-worker cap')]:text+=f"- {label}: **{100*d[key]/d['total']:.1f}%** of pooled observed time.\n"
text+=f"\nZero birth budgets occur in {sum(g['duration'].get('budget_zero',0)>0 for g in games)}/96 games; wanting more swarms while allocating zero birth labor occurs in {sum(g['duration'].get('demand_more_with_budget_zero',0)>0 for g in games)}/96. Two-thirds of zero-budget exposure coincides with the explicit food-service safeguard; the remainder lacks that trigger and requires the other budget caps to explain it.\n"
text+=f"\nBefore the first observer snapshot with at least three enemy warriors near the colony, budgets of at most two account for **{100*pre['budget_at_most_two']/pre['total']:.1f}%** of exposure, and zero budgets account for **{100*pre['budget_zero']/pre['total']:.1f}%**. Most severe throttling follows warrior pressure. This cutoff does not exclude earlier explorer harassment or isolated attacks. The cuts can amplify a losing economy, but the data does not establish them as the original cause of the entire early deficit.\n"
z=[e for g in events for e in g['zero_assignment_with_positive_budget']]
text+=f"\nDetailed staffing logs contain {len(z):,} assignment changes to zero workers while the colony budget remained positive, across {sum(bool(g['zero_assignment_with_positive_budget']) for g in events)}/96 games. Of these, {sum(int(e['farm_capacity'])>0 for e in z):,} had positive local corn-capacity scores. These are repeated decisions, not independent buildings or a measure of idle duration. They confirm that the allocator deliberately leaves some usable swarms unstaffed.\n"
text+='''
## What the early data does and does not show

At tick 10,000 Maxima has more swarm labor enrolled than Nicowar (16.4 versus 8.9), yet less population (40.6 versus 63.8). A single late staffing snapshot cannot explain the earlier accumulated gap. At tick 5,000 Maxima has only one completed swarm according to its detailed telemetry; expansion/construction timing and corn delivery need attention alongside later budget cuts.

Population changes combine births, deaths and conversions. The saved logs do not directly count births or per-swarm delivered corn, so they cannot establish production efficiency or the optimal swarm count. The observer's corn-route distance/reachable fields are zero for both AIs here and are not usable evidence of a food-access failure. Local farm-capacity events and stored corn are separate, more informative measurements.

## Statistical checks

Differences below are Maxima minus the mean of its three Nicowar opponents, with 95% intervals from 16,000 bootstrap draws clustered by seed:
'''
for t in (15000,20000):
 for k,label in [('swarm_assigned','requested swarm workers'),('swarm_working','enrolled swarm workers'),('swarms','swarm buildings/sites')]:
  r=ci[f'{t}_{k}'];lo,hi=r['seed_cluster95'];text+=f"- Tick {t:,}, {label}: {r['difference']:+.2f} [{lo:+.2f}, {hi:+.2f}].\n"
text+='''
These are descriptive AI comparisons, not randomized tests of a particular economic fix. The four rotations of each seed remain together. Snapshot carry-forward time shares weight longer surviving games more heavily.

## Recommended direction

Make new swarm construction depend on a sustained, fundable production plan: existing useful swarms should have enough food and labor before adding capacity. Then examine the birth-budget controller and inn service together. Raising the worker cap alone could worsen food pressure; simply reducing the swarm cap could slow an opening that already expands late.

The next targeted telemetry should record per-swarm corn deliveries, births, requested/enrolled carriers and precise birth-pause reasons. A controlled economic test can then distinguish slow expansion, poor placement/hauling, and excessive throttling without changing all three at once.

Artifacts: `analysis.json` and `statistics.json` hold summaries; `checkpoint-rows.json`, `per-game.json`, and `swarm-events.json` retain the extracted evidence. Original logs remain in the reserve-floor-6 tournament directory.
'''
(p/'report.md').write_text(text)
print('Wrote',p/'report.md')
