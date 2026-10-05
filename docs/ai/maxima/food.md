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
Transient units do not affect this standing estimate. Immutable profiles describe
mechanical recipe and shared-seat ceilings. Production demand is the recipe cost
divided by its complete job duration; it is not permanently throttled to a new
building's initial carrier request at a generic maximum route length. Existing
staffing feedback allocates actual workers, and placement prices local hauling.
A separate planned hauling estimate bounds usable feeding throughput and shares
one carrier allowance across all resource-consuming services on that building.
Free services consume no hauling allowance.

A nominal production mix gives each permitted output equal job weight and uses
the sum of complete job durations. Training forecasts separate recipient classes
and exclude unlearnable or rule-disabled courses. Stock consumption and carried
packets are distinct: storage multipliers convert consumption into hauling and
farm claims. Feeding and production costs remain separate components on hybrids;
`food.inn_demand_percent` and `food.swarm_demand_percent` scale their respective
wheat claims, without changing the mechanical recipe rates.

Recurring recipient demand uses each saved unit's hunger decrement, hunger
trigger and movement performance. Its stable nominal workload is travel with
equal cardinal and diagonal direction weight, using the engine's quantized action
clock. The meal cycle adds the configured approach distance, entry/exit actions,
and one service pause from the fastest available initial feeding provider for
that class. It does not use a unit's transient inside speed or current hunger
deficit: a recently fed or currently eating unit still has recurring needs.
Higher-precision rates are summed before rounding per recipient class and colony.
Future recipients use the loaded race's base unit properties. This preserves
historical map-specific hunger rates and movement tables.

The same admitted-recipient rates convert planned visits per tick into headcount
for capacity, headroom and retirement checks. The existing reliability margin
remains a strategy policy. These rates are workload forecasts, not guarantees of
static survival under every mixture of idling, working, travel and combat.
`food.ticks_per_meal` remains accepted for historical configurations and diagnostic
comparisons; it no longer supplies the live recipient clock.

## Claims and placement

The ledger is rebuilt from current buildings, explicit upgrade stages, planned
orders, population and wheat. Feeding throughput is a ceiling, not a new source
of demand for every building. Each colony counts its workers, explorers and
warriors once. Overlapping worker-production catchments form connected colonies;
units join the nearest representative. The saved recipient properties convert each
class into a recurring meal budget. Available providers divide that budget according
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

Production targets are capped by supplied worker-producing buildings plus capacity
reachable from the best available site. Birth funding sums their claimed
production-only packet rates and reachable residual crop yield, then converts
that rate to acreage using the configured regrowth period. Independent hybrid
feeding or training costs and buildings without worker production do not create
birth funding. Free or non-wheat recipes do not invent wheat acreage. Feeding demand also responds to observed hungry and
unserved units: an optimistic nominal throughput forecast must not impose a
second coarse limit on additional feeders. Each proposed location still needs
its allocated meal claim covered by reachable farm supply. Scattered residual
supply that no single building can reach does not fund another building.

Activity and service pauses change actual meal timing. A planned carrier
allowance counts concurrent hauling work rather than a fixed set of workers who
never need food themselves. Validation therefore separates historical
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
