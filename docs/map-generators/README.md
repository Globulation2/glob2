# Map generators

Choose a landscape, create reproducible maps, or develop a new generator. Lobby,
editor and study tools share registered controls; uniform terrain is editor-only.
A normal lobby rolls a fresh map and launches the previewed snapshot.

## Start here

1. [Generator catalog](catalog.md): browse every registered landscape and its design guide.
2. [Map tools](cli.md): generate, preview, render and import/export maps.
3. [Game rules for map design](game-rules-for-map-design.md): terrain, supplies, economy and routes.
4. [Add a generator](adding-a-generator.md): module and registration workflow.
5. [JavaScript generators](javascript.md): portable local and online packages.

## Implementation references

| Reference | Use it for |
| --- | --- |
| [Lifecycle and controls](map-generator-framework.md) | Requests, named randomness, generation stages and search domains. |
| [Shared toolkit](toolkit.md) | Geometry, topology, terrain, planting and site operations. |
| [Resources and starts](resources-and-starts.md) | Amount controls, fertility and colony placement. |
| [Recursive layouts](fractal-maps.md) | Subdivision, Hilbert paths and crossings. |
| [Constraint search](constraint-search.md) | Composing bounded search with constructed guarantees. |
| [World atlas](world-atlas.md) | Real-geography sources and regeneration. |
| [JSON report](report-format.md) | Final-map metrics and schema. |
| [Generator telemetry](telemetry.md) | Internal choices and bounded measurements. |

## Evaluate a design

Begin with [verification](verification.md), then measure [fairness tournaments](fairness-tournament.md)
and understand the [fitted fairness model](fairness-model.md). Recursive layouts have
[additional verification guidance](recursive-verification.md). Distributed studies
use the shared [tournament coordinator](../tools/tournaments.md).

A static score, generation success and AI outcomes answer different questions.
Inspect previews and actual play; human enjoyment remains a playtest judgment.
Run outputs belong in ignored `artifacts/` and review evidence, rather than these guides.

## Reuse a starting map

[Repeat a map](repeat-maps.md) explains tiled geography, colony dealing and supported content.
