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

## Configuration

`data/maxima/base.strategy` supplies every parameter. Match format selects one
additional file: `duel`, `ffa3`, `ffa4`, `ffa5plus`, or allied `2v2`. Files use
`key = value` assignments, with integer or boolean values and `#` comments.
The base must be complete; format files and additional layers can be partial.
Unknown keys, duplicate assignments within a file, invalid values, and inconsistent
related bounds are rejected with a source location.

`--maxima-base FILE` chooses another complete base. Repeat `--maxima-layer FILE`
to apply additional layers in order after the match-format file. All clients
simulating a new multiplayer match must use the same strategy files and options.
Saved games include the resolved strategy and Maxima execution state, so changing
a local file does not change the strategy of a resumed game.

## Validation

Build the game with `scons --build=build release=1 -j4 build/src/glob2`, then run
`python3 test/run_maxima_implementation_regressions.py --build-dir build`.
The runner covers standalone policy modules, engine order integration, building
identity reuse, configuration errors, and deterministic saved-game continuation.
Use `--test NAME` to select one regression program. Tests create temporary binaries;
they need no external services or tournament tooling.
