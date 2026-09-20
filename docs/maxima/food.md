# Food capacity

The food ledger assigns protected farm supply to inns and swarms, then exposes
unclaimed capacity to the placement planner. Only protected cells currently
holding wheat supply recurring capacity. A cell's rate is proportional to exact
fertility and the fraction of neighbouring cells that can absorb growth:

```text
yield = fertility / 65536 / food.growth_period_ticks
        * absorbing_neighbours / 8
```

Full resource stacks, buildings and unsuitable terrain cannot absorb growth.
Transient units do not affect this standing estimate. Swarm demand comes from
the engine's wheat cost and production period; inn demand uses modelled service
capacity and `food.ticks_per_meal`. These rates are configurable estimates.

## Claims and placement

The ledger is rebuilt from current buildings, levels, planned actions and wheat.
Claimants rank by supply-weighted route-distance quality band, then level,
completion status and building ID. Quality is evaluated independently of other
claimants to avoid circular ranking. Inns and swarms alternate claims; each
claims its nearest supply first. Destroyed buildings release capacity, upgrades
claim their new demand, and lost or regrown wheat changes supply immediately.

New inns and swarms require unclaimed reachable capacity at the configured
placement margin. The first building of each kind is exempt so a colony can
start before it has established farms. Independent colony swarms use their
own settlement rule. Upgrades release and retake their building's claim at the
new level. A summed-area upper bound rejects impossible candidates before the
exact route search.

Targets are capped by supplied buildings plus the capacity reachable from the
best available site. Scattered residual supply that no single building can
reach does not fund another building.

## Retirement and relocation

A building persistently below its coverage threshold can be retired after a
confirmation period. Recovery clears the timer. Retirement requires a safe
colony, elapsed cooldown, another building of the same kind, and enough reliable
inn seats for the population. Establishing colony swarms are protected.

Persistent poor route quality can instead nominate one inn or swarm for
relocation. The planner evaluates a replacement with the old building's claim
released. It prices construction and saved hauling in worker-ticks, requiring
a payback within the configured horizon plus a minimum distance or coverage
gain. The replacement is built before the old building is removed, allowing
even the sole inn or swarm to move without losing service first.

The old building and its replacement are exempt from burden retirement during
the handover. Completion rechecks the live replacement, food safety and reliable
inn seats. Failed construction, lost replacements, exhausted placement choices
or expired offers abandon the attempt and restart the cooldown. Queued deletions
are excluded from capacity checks, and completed or abandoned attempts detach
planner relationships so a later attempt can start independently.

The `food.*` parameters control supply and demand rates, margins, quality bands,
coverage thresholds, confirmation periods, cooldowns, carrier costs and relocation
payback. See [configuration](configuration.md) for the complete schema.
