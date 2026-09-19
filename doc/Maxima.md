# Maxima

Maxima is a standalone AI available in the normal player-selection menu and
through the AI name `maxima`. It uses the same order interface as other AIs.
Existing AI identifiers remain unchanged.

## Strategy and decisions

A strategy director observes the economy, available forces, enemy sightings,
and terrain. It allocates budgets for development, food production, defense,
reconnaissance, and attacks. Policy modules execute those budgets through a
private runtime that tracks buildings, gradients, queued orders, and lifecycle
conditions. Building lifetime identities prevent stale work from attaching to a
new building that reuses a previous building's slot or address.

The farming module protects sustainable food sources and reserves access routes.
The [food ledger](MaximaFoodLedger.md) accounts that protected farm capacity
against what inns and swarms actually consume, so they are only placed and
upgraded where wheat can back them, and buildings that stay far from their
wheat or under-supplied are rebuilt at a better site when that pays back, or
retired when no such site exists. [Staffing](MaximaStaffing.md) is then each building's own business: an
inn or swarm adds or returns one carrier at a time to keep its own wheat stock
inside a band, and never asks for more while it is not receiving what it
already asked for. The placement planner evaluates construction and upgrades
incrementally, then revalidates the selected action against the live world. A
building being upgraded is raised to high worker priority for as long as its
site is live, and returns to normal priority when the upgrade completes: a
half-finished upgrade serves nobody, so it outranks its equals until it is
done. Combat policy is
relentless: whenever enough eligible warriors can reach a remembered enemy
building or a visible worker cluster, one war flag sits on the best target and
moves only when that target falls or a clearly better one appears.

A proposed redesign of staffing and construction around a single labour budget
is recorded in [Maxima labour economy](MaximaLabourEconomy.md); it is a design,
not current behaviour.

Maxima uses surplus builders for tower fortification as its last construction
priority. Once the existing tower demand is covered, it can request another tower
when idle workers minus the training reserve meet the configured large-site crew
(default six). It assigns at most twelve of those spare workers. Every viable
ordinary build, repair and upgrade takes precedence, and the fallback does not
spend placement-scan updates delaying that ordinary work. Normal construction
quotas, placement legality and farm protection still apply.

This is a current-opportunity rule: unmet needs blocked by prerequisites or siting
do not prevent a tower, and temporary early-game surplus can trigger it. Existing
sites are not automatically preempted when new needs appear. Tower repairs and
upgrades subsequently use the ordinary planner. This adopted strategy does not
imply a demonstrated win-rate improvement; the paired defense study found mixed
results. [Hospital spike measurements](../docs/validation/maxima-hospital-spikes/README.md)
quantify one remaining capacity pressure.

## Configuration

`data/maxima/base.strategy` supplies every parameter. Match format selects one
additional file: `duel`, `ffa3`, `ffa4`, `ffa5plus`, or allied `2v2`. Files use
`key = value` assignments, with integer or boolean values and `#` comments.
The base must be complete; format files and additional layers can be partial.
Unknown keys, duplicate assignments within a file, invalid values, and inconsistent
related bounds are rejected with a source location.

`GLOB2_MAXIMA_BASE=FILE` chooses another complete base, and
`GLOB2_MAXIMA_LAYERS=FILE;FILE` applies additional layers in order after the
match-format file. `GLOB2_MAXIMA_FORMAT` names the format instead of inferring it,
and `GLOB2_MAXIMA_TELEMETRY=1` prints per-decision `MAXIMA_TELEMETRY` lines on
stdout. Maxima reads these itself; the game binary has no Maxima options. All
clients simulating a new multiplayer match must use the same strategy files and
settings.
Saved games include the resolved strategy and Maxima execution state, so changing
a local file does not change the strategy of a resumed game.

## Validation

Build the game with `scons --build=build release=1 -j4 build/src/glob2`, then run
`python3 test/run_maxima_implementation_regressions.py --build-dir build`.
The runner covers standalone policy modules, engine order integration, building
identity reuse, configuration errors, and deterministic saved-game continuation.
Use `--test NAME` to select one regression program. Tests create temporary binaries;
they need no external services or tournament tooling.
