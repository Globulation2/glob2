# Combat and reconnaissance

Maxima observes enemies through fog-visible units and remembered sightings.
Current visible forces are always a lower bound on estimated strength. Passive
observation operates throughout the game; active reconnaissance and economic
watch missions begin at `recon.active_min_explored_percent` map discovery. Mission limits,
reachability, target value and cooldowns govern explorer assignments.

## Force estimates

The default fitted model estimates warrior count and combat power from observation
history. The count estimate feeds opponent assessments; combat power gates new
attacks. Power uses attack damage × attack speed × current health / maximum
health, with at least one power per living warrior. Estimates round to the
nearest integer.

`AIMaximaForceModelData.inc` contains separate count and power models, each with
four quantile forests (0.10, 0.50, 0.90, 0.95), 100 trees per forest and at most
seven leaves per tree. Integer thresholds and fixed-point leaves at scale 1024
keep inference deterministic. Quantiles are sorted and power is bounded below
by warrior count. Median predictions drive decisions; upper quantiles do not
set the attack gate.

Inputs are visible and remembered forces, sighting ages, visible unit classes
and power, known and visible buildings, reconnaissance confidence, explored
percentage and AI tick. History adds previous values, peaks and rates of change
for visible warriors, workers, power and known buildings, plus elapsed time since
the observation. Sighting ages are clipped; negative rates use floor division.
No hidden enemy units or global truth enter the model.

Observation history refreshes on the first strategic review and then at the
model's observation interval. Reviews forecast to the present; lightweight
samples retain the prediction and enforce visible lower bounds.

## Army demand and attack waves

Army demand grows from the largest estimated enemy force by
`1 + game_tick / (100 * assault.force_growth_ticks)`, rounded up. A warrior
floor and an independent `game_tick / 1000` backup demand protect against
incomplete reconnaissance; Maxima uses the larger target. The engine unit limit
bounds demand. Food, labour and training capacity constrain actual production.

Eligible warriors are those meeting the flag level requirement and available
for offense. Usable barracks reservations take precedence: each warrior and
training slot is reserved at most once, and fully trained warriors are not held
back by seats they cannot use. Surplus warriors can attack a remembered enemy
building or visible worker cluster. Target value, nearby towers, route distance
and persistence govern selection. An eliminated opponent releases its siege
progress lock.

Land attacks gather on connected open ground near a completed friendly inn or
swarm. Rally placement excludes buildings, resources, water and forbidden tiles,
requires space for a full wave, and prefers spare room for arrivals and workers.
The default gathering radius is four tiles; saved strategies retain their stored
radius. If construction blocks a gathering point, the next review relocates it
when another suitable site exists. A land colony with no room waits rather than
sending an ungathered wave directly to the target.
One cohort recruits at a time while earlier waves advance. Recruiting flags have
higher priority. A wave launches when its configured share has arrived within
the circular rally radius plus a two-tile arrival tolerance. The tolerance counts
nearby warriors without expanding the flag or the attack objective radius. After
stalled progress or the maximum assembly time, a smaller wave may launch if it has enough force and enough of its enrolled warriors have
arrived; otherwise it is released. Recruitment budgets may grow during assembly.
These clocks use Maxima's AI ticks.

Advancing flags follow the objective, reduce recruitment as members leave and
expire after remaining empty for the inactivity interval. Travel and assembly
do not consume the siege-stall allowance. An amphibious attack uses the
streaming controller when no connected land rally is available.

After four consecutive failed waves, Maxima switches to its existing
streaming attack controller for the rest of the game. A wave is assessed once its
enrollment falls to at most one quarter of its launch force, or it is retired.
Its peak simultaneous healthy enrollment within the objective's siege radius must
reach 33% of the launch force; success resets the failure counter. A rally that
reaches the existing maximum assembly time without launching also counts as a
failure toward the same counter. No map-specific checks or extra timeouts apply.

Streaming divides the same available attack force across persistent flags, using
the configured per-flag troop cap (20 by default): 45 available warriors request
three flags with 20, 20 and 5 places. It adds or removes flags as the force changes,
and continuously replaces departing warriors. Additional flags are created near
the objective with overlapping attack ranges. Target changes move all retained
flags; losing one flag does not end the other flags' attack. Training and defense
reservations still apply.

Delivery observations, the counter and the permanent switch survive save/reload.
When loading a format-115 save, already advancing waves are excluded because their
launch force is unknown. This fallback does not detect attacks blocked before
a rally is created.

## Forward support

`forward_support.inns_enabled` and `forward_support.hospitals_enabled` enable
dedicated feeding and healing construction during an established attack. Both
default to true for new games. This pathway uses its own placement purpose; ordinary inns,
hospitals and their campus scores keep their existing criteria.

A forward site lies within eight tiles of the configured
`forward_support.distance_tiles` distance (40 tiles by default) from the current objective, measured
with wrapped Chebyshev distance. The minimum distance is eight tiles. Its
footprint must have no currently observed threat, and normal terrain, builder
access, routes, food backing, reservations and construction limits still apply.
The site is revalidated against the live objective before issue. This is an
annulus around the objective, not a guarantee of placement on its home-facing
side or of future safety.

Forward requests require at least 12 warriors, 24 workers, an attack older than
the dwell interval, and no economic recovery or severe colony emergency.
Ordinary development and maintenance take precedence without waiting for a
forward-site scan; forward support precedes spare-labour fortification. Existing
nearby providers, pending requests and a limit of two completed forward providers
per role constrain demand. Completed buildings use the normal staffing and
service controllers. Hunger-disabled games omit feeding requests; peaceful games
omit both. Building capabilities and availability govern the selected types.

## Fruit supply

For each known fruit variety, Maxima searches for a reachable deposit from each
completed inn. Routes span the map, wrap, include diagonal moves, and respect
buildings, resources, forbidden areas and current swimming capability. The field
is rebuilt at each fruit-management pass. Distinct varieties count once; inns
with more reachable varieties take priority, with stable iteration order breaking
ties. The nearest-deposit approximation can miss a farther patch already in vision.

At most one explorer flag serves each useful variety lacking completed-building
vision. Pending missions are reused, moved when sources change and released when
building vision covers their work. Lost vision restores demand. Defence and
recovery do not automatically cancel supply, and there is no population gate.
Useful visible, building-covered or stocked supply keeps enemy fV inn advertising
available. Fruit collection does not enable enemy mV. Disabling `fruit.enabled`
removes these missions and their advertising.

Related: [AI documentation](../README.md).

`forward_support.coverage_distance_tiles` suppresses a new request when an
existing provider is within 40 geometric tiles of the objective. It is independent
of the proposed site's distance. A site around a 40-tile ring may be on any side
of the objective and may require a much longer walking route. Route estimates in
telemetry are diagnostic; they do not change the selected geometric policy.

Target commitment gives objectives against the current enemy a bounded score
bonus (`tactics.enemy_commitment = 1`), rather than locking the enemy choice.
Arrival grace (`tactics.arrival_grace_ticks = 4000`) delays stall judgment while
an effective force is still arriving, up to its configured limit. Existing home
emergencies and invalid-target handling retain their authority.

When an offensive flag already exists, objectives receive a penalty based on
their wrapped Chebyshev distance from that primary flag's actual position.
`tactics.flag_proximity_weight = 4` subtracts four score points per tile, up to
32 tiles (128 points). This favors continuing through nearby buildings and raid
locations without locking the objective: a sufficiently better distant target
can still win. Existing reachability, quarantine, dwell and emergency checks
remain authoritative. New attacks with no existing flag receive no penalty;
zero weight disables it. This is a geometric preference, not a walking-route
estimate.

Saved games from before format 188 retain their stored tactical behavior. Missing
forward-service and continuity controls restore the former disabled defaults;
loading an older game does not silently opt it into the new policy.
Format-188 strategies also retain their previous behavior when the proximity key
is absent; format 189 writes that key explicitly.
