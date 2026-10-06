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
mechanical recipe and shared-seat ceilings. A production ceiling is the weighted
recipe cost divided by complete job duration, including the completion tick.
The operating farm claim is separate: it reserves the production the requested
carriers can supply along the provider's sustained supply routes. The existing
harvesting reach walk yields a cumulative packet/work curve, independent of
other buildings' claims. Independent wheat demand and production consume that
same curve once; a nearby low-yield cell cannot price the entire recurring flow
at its distance. Demand beyond reachable yield receives the existing unreachable
route penalty, so an isolated producer retains positive demand and poor coverage. Feeding, healing,
training and ammunition spend the shared carrier budget first; discretionary
production uses the remainder. Those independent services retain their recurring
claims during a shortage. Free services consume no hauling allowance.

Completed providers use their requested staffing after the controller's shared
producer allowance, and their active output ratios. Empty stores and temporarily
absent workers do not erase demand. New providers and upgrade targets use the
strategy's initial staffing request, clamped to the target's assignment limit;
a presentation default is only a fallback where the strategy has no request.
A separate planned hauling estimate bounds usable feeding throughput. Neither
that forecast nor the operating farm claim changes the mechanical capability
ceiling used to compare recipes.

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
results. Each candidate's existing exact residual walk also gathers its
uncontested supply curve. Feasibility, scoring and relocation reuse that query;
there is no additional route walk or catalog scan. Cumulative prefixes support
bounded arithmetic queries while solving the common production fraction. Other
resources use the existing prepared distance fields and unreachable penalty. Profile, requested staffing, output ratios and colony demand state
are included in planner signatures and continuation.

Claimants rank by supply-weighted route-distance quality band, then level,
completion status and building ID. Quality is evaluated independently of other
claimants to avoid circular ranking. The established rank is retained within two phases: independent services
reserve their nearest supply first, then discretionary production claims the
remainder. This explicit precedence prevents births from taking already-needed
meals when feeding is split across more buildings. Destroyed buildings release capacity, upgrades
claim their new demand, and lost or regrown wheat changes supply immediately.

Candidates reserve independent services from the pre-production supply layer;
their production component must fit genuinely unclaimed supply after those
services. Both components use the configured placement margin without spending
a packet twice. Redistributing the same meals can transfer a geographically
reachable, already-funded reservation: only that transferred amount waives extra
margin. Unfunded demand and remote reservations supply no credit. The first building of each kind is exempt so a colony can
start before it has established farms. Independent colony swarms use their
own settlement rule. Upgrades release and retake their building's claim at the
new level. A summed-area upper bound rejects impossible candidates before the
exact route search.

The existing birth controller chooses production targets; exact local feasibility
gates ordinary creation and upgrades, including revalidation before issuing the
order. Colony settlement retains its separate new-land rule, because its farms
do not yet have protected supply. Pending projects already claim in the ledger.
The `food.target_capping_enabled` heuristic also caps producer targets by existing
worker producers with sufficient uncontested reachable supply, plus residual
supply divided by the candidate's runtime-derived peak recipe cost. Existing
capacity does not disappear from this count merely because feeding takes priority
in the current allocation; ordinary new sites still need their actual local claim
funded. Wheat-free worker production is not constrained
by this wheat count. The existing growth reservation remains in place; local
feasibility is necessary for every ordinary site.
Birth funding sums their claimed
production-only packet rates and reachable residual crop yield, then converts
that rate to acreage using the configured regrowth period. Independent hybrid
feeding or training costs and buildings without worker production do not create
birth funding. Free or non-wheat recipes do not invent wheat acreage. Feeding demand also responds to observed hungry and
unserved units: an optimistic nominal throughput forecast must not impose a
second coarse limit on additional feeders. There is no crop-tiles-per-feeder
limit, including when the optional food ledger is disabled; policy construction
caps remain in force. With the food ledger enabled, each proposed location needs
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

Retirement distinguishes allocation from site viability. For a building with a
production demand, the coverage signal compares its operating demand with all
recurring supply it can reach, before competing providers claim it. A temporary
production shortfall caused by feeding precedence is not evidence that demolishing
that producer will help. A site that cannot reach sufficient supply even without
competition can still be retired. Non-producing services retain their actual
allocation-based coverage signal.

This uncontested supply is a decision signal, never another resource budget.
Production funding, candidate feasibility and resource conservation still use
actual claims and residual supply. It is computed during the existing route-quality
walk without another traversal or catalog scan. Telemetry reports `coverage`,
`uncontested_coverage` and `retirement_coverage` separately.

A building persistently below its retirement coverage threshold can be retired
after a confirmation period. Recovery clears the timer. Retirement requires a safe
colony, elapsed cooldown, another building of the same kind, and enough reliable
feeding throughput for every admitted recipient class. The capacity check tests
all seven subsets of the three unit classes against shared provider rates, so
excess worker-only seats cannot justify removing the only explorer feeder.
Establishing colony swarms are protected. Independent training or combat services
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

## Comparing strategy changes

Separate a policy change from a rules-engine migration. On one engine revision,
compare the policy variants with identical maps, seeds, opponents and swapped
starting positions. Across engine revisions, include an unchanged passive
controller as well: changes to another AI can affect Maxima's access to resources
even in peaceful games. Keep those controls separate from competitive matches.

Choose the objective before tuning: preserving historical decisions and improving
competitive strength are different goals. For strength tuning, use competitive
outcomes as the primary measure and development timings to diagnose trade-offs.

Freeze a candidate before the held-out seeds. Measure population trajectories,
final population, starvation and competitive outcomes separately, and retain failed
runs. Treat a seed/map pair as a statistical unit rather than treating each tick
as an independent observation. Report uncertainty and map-specific variation;
a similar mean or a few extra wins do not demonstrate equivalent strength or an
optimal strategy. Keep temporary results and reproduction commands in ignored
review evidence, as described in the repository contribution instructions.
