# Economy and staffing

Maxima balances food delivery, births, building work and training through a
shared labour budget. Observations count workers eating, healing, training,
hauling, building or idle, together with usable training seats and requests.
The opening leaves swarm requests untrimmed. Afterwards the budget reserves
training slack, subsistence and construction labour, then caps swarm carriers
by the remaining workforce and food-funded birth allowance. A production floor
protects continued growth. When requests exceed the cap, the largest swarm
requests are trimmed first while preserving their minimum staffing.

Workers need idle time to enter training. The reserve therefore counts seats
that an untrained worker can actually use, rather than every training slot.
Production demand also accounts for the backlog of warriors awaiting barracks
training. Explorer production remains available while the colony is below its
target; a zero birth budget pauses all production ratios.

While the army is below target, an established workforce that covers staffed
jobs and outstanding requests, plus one relief worker per three jobs and the
training reserve, stops adding workers. Food-funded births then prefer warriors.
If the military training backlog is full, that pause does not redirect production
back into surplus workers. A genuine workforce shortfall restores worker births.

Training capacity scales toward one seat per four desired warriors once the
workforce reaches `military.second_barracks_population_min`. Existing barracks
sites receive credit for their finished capacity; the old five-barracks ceiling
does not apply. Births allow two untrained warriors per committed seat, subject
to `military.training_backlog_floor`. Food still funds the total birth allowance
and construction still competes with food service and other development.

Food funding retains fractional fertility until the final worker allowance is
rounded. Positive funding keeps at least one producer. If every local food
catchment is empty, a shared search from completed inns and swarms looks for
reachable growing wheat and discounts remote supply by route distance. Stored
wheat alone is not recurring supply.

Custom game rules remove unavailable work before budgets are allocated. With
upgrades disabled, training reserves and barracks expansion disappear, and
recruitment accepts the standing army. Hospitals and existing barracks still
provide healing. Without hunger, feeding demand does not suppress growth, while
swarms still need wheat for production. Resource estimates scale renewable yield
by scarcity and count only standing supply when regrowth is disabled. Resource
searches and production remain useful until that stock is depleted. Existing
farming protection is released without regrowth, so seed reservations cannot hide
finite grain forever. Peaceful rules remove military bids before arbitration,
including warrior births and combat flags; economic budgets remain active.

The capability gates precede demand scoring, posture selection and labour
arbitration. Executor checks also drain unavailable saved requests, but cannot
replace those planning gates: an impossible bid would otherwise consume labour
and suppress feasible development. New rules should be represented in these same
stages rather than stored as a separate persistent strategy profile.

## Prestige race

With normal prestige victory active and `prestige.enabled=true`, the policy
competes for the same construction and training budget as ordinary technology. It
prepares a prestige-producing building, invests in its upgrade chain, prioritizes
a winning
finish, or holds when prerequisites are missing, survival needs intervene, or
it cannot catch the surviving leader before crossing the shared threshold.
Victory enablement, the threshold and alliance-sharing precedence come from
primitive snapshot metadata; the copied configuration omits victory objects.
Catalog prestige values determine the increments; the policy does not assume a
fixed school score. Dedicated investment uses upgrade chains whose prerequisite
buildings and construction sites have zero prestige, so cancelling an upgrade
cannot itself add prestige. A catalog without such a route leaves this policy
on hold. It credits
completed buildings and pending construction, including prestige due from a new
building's first completion,
requests one to four unfinished pipelines according to spare qualified builders,
and keeps ordinary school demand when that is larger. `prestige.max_pipelines`
defaults to four; set it to one for the conservative comparison policy.
Each allocation assesses prestige after initializing
the director budget, so only completed prestige-producing levels retire a pipeline;
basic schools, prerequisite upgrades and pending sites continue to supply it.
Healthy idle workers qualified for the final upgrade, plus qualified builders
already working on that building lineage, supply the parallel allowance.
Existing training reserves and unmet ordinary construction requests are deducted;
capacity above the base pipeline needs full configured construction/upgrade crews.
The base request still passes ordinary workforce and funding checks. Counting builders
already on the pipeline prevents its own staffing from shrinking its allowance.
The allowance also stops at the number of remaining prestige increments needed,
and an immediate winning finish uses one pipeline. Private materials in an
existing school cannot justify extra new pipelines without supply for their full
upgrade chains. Population, qualified-builder, service continuity, placement and
construction limits still apply. The requested stock is capped at the engine's
per-team building limit.
New matches enable this policy by default when normal prestige victory is active.
It can increase spending on schools and qualified builders when prestige is viable;
set `prestige.enabled=false` to opt out.

The threshold counts all teams' prestige, including defeated colonies. Leadership
considers surviving teams individually. Tied leaders win; an ally's individual
lead is sufficient only with mutual alliance and the alliance victory condition
ordered before prestige victory. Private stock funds only its own building’s next
upgrade; unrelated private stores do not establish a construction supply route.
Completion projections include fresh individual scores between the engine's
32-tick global-total refreshes. A larger cached total remains conservative until
the next refresh after a prestige building is lost.
Before starting a prestige-producing action, Maxima rejects a completion that
would cross the threshold and award it no victory. Only one such completion is
committed at once. Fresh observations also recheck queued orders and active
upgrades. An unsafe active upgrade is cancelled with the ordinary construction
cancel order when restoring its previous building is safe; cancellation waits
for execution and the observed result before releasing its planner commitment.
Custom first-level prestige sites still block additional prestige commitments
and receive the start check, but the engine cannot undo them with an upgrade
cancel order. The policy does not demolish them. It also rejects a cancellation
that would restore enough prestige to trigger a loss in a custom catalog.

The policy changes economic investment, not combat targets, defense placement
or reconnaissance objectives. Timed prestige finishes alone do not activate it.
Turning off normal prestige victory or `prestige.enabled` removes both the
investment policy and its completion guards. Race telemetry reports numeric
state/reason, standings, remaining threshold, pending gain and investment priority.
Director snapshots also expose the selected target level, prestige pipeline request
and ordinary school request after allocation.

Save format 192 persists race budgets and cancellation identities/receipts.
Format 193 adds the adaptive pipeline setting. Saves predating that setting
restore `prestige.max_pipelines=1`, preserving the conservative execution policy.
Supported older Maxima saves gain the new policy when their match enables prestige
victory. Missing prestige configuration explicitly receives `enabled=true` and
the documented weights. The new policy reassesses pending work before development
execution resumes. Other saved execution work is retained. This intentional
behavior change requires the
new simulation/network version; it does not drop older save support.

## Local staffing

Every completed inn and swarm samples corn stock as a share of its capacity
and the workers it actually receives. Integer rolling averages drive a stock-band
controller at the building review interval:

- Below the low stock threshold, add one worker if actual staffing is close
  enough to the current request.
- Above the high threshold, return one worker.
- Otherwise hold the request.

A shortfall in actual staffing prevents repeated increases that the colony
cannot satisfy. Warm-up, cooldown, stock thresholds, slack and request limits
are configurable under `staffing.control_*`. Newly completed buildings start
with `staffing.new_inn_workers` or `staffing.new_swarm_workers`, then follow
the same loop. Swarm allowances remain bounded by the colony labour plan.
Controller state belongs to the building's lifetime and is saved with Maxima.

At each food-ledger review, completed inns and swarms are ranked separately by
existing wheat-route quality (average supply distance, including the penalty for
unreachable wheat). The worst half, rounded down, receive low worker priority;
the rest receive normal priority. Equal scores are ordered by building ID.
Construction sites and upgrades are excluded from this ranking. Ordinary inn
construction receives high priority during a food emergency and normal priority
otherwise. These priority reviews require the food ledger to be enabled.

## Development

Schools retain their early population and technology gates. Once the higher
technology gate passes, their target is the greater of the configured advanced
school target (three by default) and population divided by 70, rounded down:
four schools at 280 population, five at 350, and so on.

The placement planner compares legal construction and upgrades incrementally,
then revalidates the selected action against the live world. Demand, labour,
food backing, health, reserved space and construction quotas constrain both.
Positive-scoring upgrades receive a preference when no viable emergency
construction candidate is present. An active upgrade receives high worker
priority until completion.

For every building category, repairs and upgrades share an allowance that leaves
at least half the existing buildings operational, rounded up: two or three
buildings permit one maintenance job; four or five permit two. A lone building
may be repaired or upgraded. Unfinished new construction does not increase the
allowance. Reserved and queued jobs count before the engine starts their sites;
both repair and upgrade permissions are checked during selection and again
before issue. Existing jobs are not cancelled when loading a save or losing
another building reduces the allowance.

With multiple barracks and warriors awaiting training, upgrades additionally
preserve at least half of the
committed training seats in operation. Already-issued upgrades count as offline
before the engine observes the new site. If an upgrade quota shrinks, existing
upgrades retain their commitments without consuming the separately authorized
new-building slots; this does not authorize additional upgrades above their quota.

Hospital construction and upgrades share a target of
`military.hospital_beds_per_warrior_percent` beds per live warrior, rounded up.
Committed sites and upgrades count once; injuries raise priority without adding
a separate quantity target. Hospitals are retained when the army shrinks.
Physical beds count toward demand; healing throughput also depends on tier.

Tower fortification is the last construction priority. After ordinary tower
demand is covered, spare workers beyond the training reserve can fund another
tower. Viable ordinary builds, repairs and upgrades take precedence. Normal
quotas, siting and farm protection still apply; a committed tower site remains
ordinary construction when other demand appears.

Economic recovery restricts growth and development, while military activity
continues to follow defence needs, eligible forces, reachability, target safety
and cooldowns. Food pressure alone does not veto attacks.

Related: [AI documentation](../README.md).
