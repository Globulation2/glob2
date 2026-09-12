# Maxima staffing control

Every inn and swarm sets its own worker request from what it can observe about
itself. There is no colony-wide staffing budget, no apportionment between
buildings, and no estimate of nearby wheat.

## The loop

Once per building pass (`scheduling.building_interval_ticks`, 200 by default)
each building samples two things:

- its **stock**: `resources[CORN]` as a share of its own corn capacity, in
  thousandths — inns hold 10, 30 and 50 by level, swarms 20
- its **actual staffing**: `unitsWorking.size()`, which is what it is really
  getting, not what it asked for

Both feed integer rolling averages over `staffing.control_window_samples`
passes. Once the averages have settled, one rule applies:

```text
average stock < control_low_permille  and staffed  ->  request one more worker
average stock > control_high_permille               ->  return one worker
otherwise                                           ->  hold
```

"Staffed" means the average actual staffing is within `control_slack` of the
request. The request is clamped to
`[control_minimum_workers, control_maximum_workers]`, so **every building keeps
at least one carrier** and none exceeds the engine's ceiling of 20.

The defaults keep the stock between one third and two thirds full.

## Starting staffing

The loop is deliberately slow: it waits for a settled average and then moves one
carrier per cooldown. Starting every building at the minimum would leave a newly
built inn or swarm nearly idle for many passes, which is exactly when it is
needed. So a building that has just been built is seeded with
`staffing.new_swarm_workers` (8) or `staffing.new_inn_workers` (4) on its first
control pass, and the loop takes over from there — free to raise or lower that
number like any other. The seed applies once per building; it is not a floor,
and `control_minimum_workers` still bounds the loop from below.

## Why it is shaped this way

**Stock is the honest signal.** A building that runs dry is understaffed
whatever the map looks like; a building that stays full has carriers with
nothing to do. Measuring the stock also removes a double-counting bug in the
old method: two inns beside one farm each counted all of that farm and each
staffed up for it.

**Not receiving what you asked for is not a reason to ask for more.** If the
average staffing is short of the request, the workforce is the constraint, and
raising the request would wind up with no effect. This is the anti-windup rule,
and it is why actual staffing is measured at all.

**The engine already sheds idle carriers.** `Building::desiredNumberOfWorkers`
caps real workers at `4/3 x (capacity - stock)`, so a nearly full building gets
fewer workers than it requests no matter what. That clamp only binds above
roughly 80% fill — above the upper band — so in the region where the controller
wants to *add* a worker it never interferes, and a shortfall there really does
mean the workers are absent.

**Integral state, so it is saved.** The request is the controller's integral
term. It is persisted per building in Maxima's execution state (save version
94); a version 93 save loads without it and each building restarts its loop
from the minimum. State for a destroyed building is dropped, so a later
building reusing its id does not inherit a stranger's loop.

## Parameters

| Key | Default | Meaning |
| --- | --- | --- |
| `staffing.control_window_samples` | 8 | length of both rolling averages, in passes |
| `staffing.control_cooldown_passes` | 3 | passes a building waits after a change before changing again |
| `staffing.control_low_permille` | 333 | stock below this earns a carrier |
| `staffing.control_high_permille` | 667 | stock above this returns one |
| `staffing.control_slack` | 1 | how far below its request a building still counts as staffed |
| `staffing.control_minimum_workers` | 1 | every building keeps this many |
| `staffing.control_maximum_workers` | 20 | ceiling on one request |

## What this replaced

- The inn fertility curve, which scaled staffing from summed nearby wheat
  fertility and ignored the inn's own stock, its occupancy and hunger.
- The colony swarm-worker budget and its D'Hondt apportionment across swarms
  (`AIMaximaStaffing.h`), whose weights double-counted shared wheat.
- `staffing.inn_adaptive_staffing_enabled`, the three `inn_levelN_*` worker
  pairs, and the three `inn_levelN_low_corn_threshold` values, which no
  decision had read for some time.
- The CORN resource trackers installed on every inn and swarm, together with
  `staffing.resource_tracker_samples`. Nothing in Maxima ever read them; only
  the older Echo path consumes trackers.

Birth funding stays a colony decision: `SwarmController` still sizes the swarm
target, and a zero birth budget still pauses production ratios. Only staffing
became local.

## Tests

`test/MaximaStaffingControlStandaloneTest.cpp` covers growth when empty,
shedding to the minimum when full, holding inside the band, the anti-windup
rule, slack, the minimum and maximum, capacity normalisation between building
types, and the warm-up delay.
