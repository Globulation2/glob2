# Food capacity

The food ledger assigns protected farm supply to buildings with recurring wheat
costs, then exposes
unclaimed capacity to the placement planner. Only protected cells currently
holding wheat supply recurring capacity. A cell's rate is proportional to exact
fertility and the fraction of neighbouring cells that can absorb growth:

```text
yield = fertility / 65536 / food.growth_period_ticks
        * absorbing_neighbours / 8
```

Full resource stacks, buildings and unsuitable terrain cannot absorb growth.
Transient units do not affect this standing estimate. Production demand comes from the available recipes. Feeding capacity and
resource claims use one per-variant operating estimate: reserved-seat occupancy
includes service time and travel, while replenishment uses a separate hauling
cycle. Concurrent services share seats and all resource-consuming recipes share
one planned carrier allowance for the physical building. A nominal production
mix gives each permitted output equal job weight and uses the sum of complete
job durations. Training forecasts separate recipient classes and exclude
unlearnable or rule-disabled courses. Stock consumption and carried packets
are distinct: storage multipliers convert consumption into hauling and farm
claims. Free services consume
no hauling allowance. `food.ticks_per_meal` converts meal throughput to supported
population; coverage and the existing reliability margin then express shortages.
Temporary carrier shortages do not reduce the nominal estimate and trigger
additional building demand. These rates remain strategy estimates, not promises
that a colony can sustain the theoretical maximum.

## Claims and placement

The ledger is rebuilt from current buildings, explicit upgrade stages, planned
orders, population and wheat. Feeding throughput is a ceiling, not a new source
of demand for every building. Each colony counts its workers, explorers and
warriors once. Overlapping worker-production catchments form connected colonies;
units join the nearest representative. The nominal meal interval converts each
class count into a meal budget. Available providers divide that budget according
to admission and capacity, giving scarce classes and less flexible providers
priority. A free feeder can take meals without claiming wheat. Production,
healing, training and ammunition costs retain their independent shares on mixed
buildings.

Adding or replacing a feeder redistributes the same colony budget before farm
coverage is checked. Candidate ledgers are prepared once per colony and candidate
profile before scanning positions; spatial queries select immutable cached
results. Profile and colony demand state is included in planner continuation.

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

Production targets are capped by supplied buildings plus capacity reachable from
the best available site. Feeding demand also responds to observed hungry and
unserved units: an optimistic nominal throughput forecast must not impose a
second coarse limit on additional feeders. Each proposed location still needs
its allocated meal claim covered by reachable farm supply. Scattered residual
supply that no single building can reach does not fund another building.

The nominal meal interval was measured in ordinary mixed games. Idle or
warrior-heavy stress scenes can eat substantially more often, and a planned
carrier allowance counts concurrent hauling work rather than a fixed set of
workers who never need food themselves. Validation therefore separates historical
stock-population survival, deliberate overload measurements, and adaptive hunger
recovery against matched static controls. Reactive recovery can still lose units
before sufficient service becomes available; the recovery check also measures
whether attrition continues after that response. The forecast is not a
guaranteed static population limit.

## Retirement and relocation

A building persistently below its coverage threshold can be retired after a
confirmation period. Recovery clears the timer. Retirement requires a safe
colony, elapsed cooldown, another building of the same kind, and enough reliable
inn seats for the population. Establishing colony swarms are protected. Independent training or combat services
also prevent food-only retirement of mixed buildings.

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
