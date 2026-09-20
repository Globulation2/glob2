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
