# Configuration

Maxima resolves a complete strategy when first asked for an order or a save.
Sources apply in this order, with later assignments taking precedence:

1. `data/maxima/base.strategy`, or the complete file named by `GLOB2_MAXIMA_BASE`.
2. The match-format layer: `duel`, `ffa3`, `ffa4`, `ffa5plus`, or allied `2v2`.
   `GLOB2_MAXIMA_FORMAT` overrides automatic selection.
3. Files in `GLOB2_MAXIMA_LAYERS`, separated by semicolons.
4. Team overrides, then player overrides.
5. `GLOB2_MAXIMA_OVERRIDES`.

Files contain `section.key = value` assignments with integer or boolean values
and `#` comments. Layers may be sparse. Unknown keys, duplicate assignments
within one source, malformed values, out-of-range values and inconsistent bounds
are errors with source locations. The resolver does not silently clamp values.

Team and player overrides use scoped lists, for example:

```sh
GLOB2_MAXIMA_TEAM_OVERRIDES="0:farming.enabled=false|2:tactics.min_force=6"
GLOB2_MAXIMA_PLAYER_OVERRIDES="0:tactics.min_force=8"
```

The base file and schema are the reference for available keys and defaults.
The schema exposes types, units, descriptions, hard bounds and recommended tuning
bounds. `MaximaStrategyDump` also reports resolved values and source provenance:

```sh
MaximaStrategyDump --dump-maxima-schema
MaximaStrategyDump --dump-maxima-strategy --maxima-format 2v2 \
    --maxima-layer custom.strategy --maxima-overrides 'tactics.min_force=8'
```

The executable is built from `test/maxima/MaximaStrategyDump.cpp` by the
[Maxima test runner](../../../test/maxima/README.md).

| Parameter groups | Decisions |
| --- | --- |
| `model`, `environment`, `trends`, `demands` | Capacity estimates, observations and demand |
| `economy`, `staffing`, `upgrades`, `construction` | Production, worker requests and development |
| `food`, `farming` | Renewable supply, capacity claims and clearing |
| `military`, `postures`, `emergencies` | Army demand, service capacity and strategic priorities |
| `placement`, `scoring` | Sites, routes, candidate utility and switching margins |
| `defense`, `tactics`, `assault`, `raiding` | Defensive response and attack execution |
| `recon`, `explorer_campaign`, `fruit` | Observation, explorer missions and fruit supply |
| `scheduling` | Review intervals, phases and cooldowns |

All clients simulating a new multiplayer match must use identical resolved
settings. Resuming a saved game uses its stored strategy rather than the local
files. `GLOB2_MAXIMA_TELEMETRY=1` emits `MAXIMA_TELEMETRY` decision records;
[AI telemetry](../telemetry.md) describes the common telemetry interface.
