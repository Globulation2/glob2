# Maxima attack strategy

New games enable age-based army growth and gathered land attacks. Target ranking
still uses the existing building values, nearby-tower penalty, route distance,
worker-cluster scoring and target persistence. There is no additional defensive
commitment ramp or capacity-based building selector.

## Army demand

With `assault.force_ramp_enabled`, demand is the largest estimated enemy army
multiplied by `1 + game_tick / (100 * assault.force_growth_ticks)`, rounded up.
The default growth interval is 1,000 ticks: 100% initially, 110% at tick 10,000,
and 150% at tick 50,000. The warrior floor still applies; the engine unit limit
bounds demand. Food, labour and training capacity still constrain production.
The fitted force model supplies the enemy estimate when enabled.

## Gathered attacks

With `assault.waves_enabled`, a land attack gathers warriors at a completed
friendly building connected to the objective. One cohort recruits at a time,
with up to 20 warriors per flag and 16 simultaneous flags by default. Recruiting
flags have higher priority than advancing flags.

A full wave launches when at least 75% of its capacity has arrived within the
rally radius of three tiles. After 500 ticks without assembly progress, or at
3,000 ticks total, a smaller wave can launch if it meets the normal minimum
force and at least 75% of its enrolled warriors have gathered. A stalled cohort
that cannot meet those conditions is released. Small initial recruitment budgets
can grow while assembling. These clocks use Maxima's AI ticks.

Advancing flags follow the selected objective, reduce recruitment as members
leave, and expire after remaining empty for the inactivity interval. The next
cohort can assemble while previous waves advance. Traveling and assembling do
not consume the siege-stall allowance. Amphibious attacks retain the existing
single-flag controller when no connected land rally is available.

Attack commitment remains eligible warriors minus usable training reservations.
Both assault controls can be disabled independently through the strategy schema.

## Compatibility and regression coverage

Save format 112 records wave identity, phase, recruitment, locations and assembly
clocks alongside the execution queue. Production saves through version 111 retain
their original assault behavior; missing assault controls default off. The minimum
readable save format remains 58. Network protocol 37 excludes older peers because
AIs execute locally; recorded-order replay acceptance retains its floor of 99.

The discarded private experiments reused versions 110–112 before consolidation;
their files are not production-format fixtures and were deleted during cleanup.

`MaximaCombatIntegrationTest` covers gathering versus enrollment, partial-cohort
recruitment, successive waves, flag deletion, idle scheduling and serialized wave
continuation. `MaximaTacticsStandaloneTest` covers army arithmetic and assembly
thresholds; `MaximaStrategyTest` covers current defaults and legacy migration.
