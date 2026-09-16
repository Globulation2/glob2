# Generator telemetry

The JSON map report includes `generation.telemetry`: observations supplied by the generator and its shared helpers during the attempt. These explain **how** a particular map came about. The existing terrain, resources, movement and quality sections analyze **what** the completed world contains. A planned island count and the final number of land components are different measurements; neither replaces the other.

## Collection and cost

`GenerationContext` owns a `GenerationTelemetry`; `GenerationService::generate(game, request, true)` collects it and returns it in `GenerationResult`, including on failures. Collection defaults to **off**. Normal lobby/editor generation, candidate selection and validation reconstruction do not collect records. The CLI opts in when `--generate-map` has `--json`. Loading an existing map cannot reconstruct the original trace, so its telemetry is `null`.

Telemetry must not consume random numbers, change search order, branch decisions or world state. Record values already computed by the algorithm and cheap counters in existing loops. Do not add pathfinding, fertility calculations, grid scans, serialization, timing, or file I/O to generator instrumentation. Guard telemetry-only summaries or dynamically constructed strings with `context.telemetry.enabled()`. The literal-key API uses string views and disabled calls do not allocate records or strings. Histograms, when needed, should aggregate once over a small feature list rather than scan the map for each category.

Collection has a fixed limit of 4,096 records per attempt. Keys are limited to 128 bytes, text values to 512 bytes. Excess records or oversized/empty keys/text are dropped, never silently renamed or truncated. `dropped_records` exposes that loss; non-finite or out-of-range numeric observations increment `invalid_values`. Do not interpret a limited trace as complete. These bounds apply to retained observations, not permission to emit a per-tile event stream up to the limit. Prefer a count, histogram or occasional feature record.

No JSON serialization runs inside generation. Export happens after the attempt. The normal final-map analysis can cost more than collecting these observations; benchmark generation separately from report analysis and PNG export.

## API and record meaning

```cpp
context.telemetry.measure("example.islands.requested", wanted);
context.telemetry.measure("example.islands.placed", islands.size());
context.telemetry.choice("example.heart.kind", "orchard");
context.telemetry.measure("example.farm.target_sites", targetSites, team);
context.telemetry.measure("example.farm.actual_sites", actualSites, team);
if (actualSites < targetSites)
    context.telemetry.fallback("example.farm.shortfall", "eligible ground exhausted", team);
```

Every record has `key`, `kind`, `subject`, and `value`:

- `measurement` keeps integer, floating-point or boolean values as their JSON types. Do not put a number and its unit into a string.
- `choice` records a stable named variant. Record the effective variant after fallback; record the rejected choice too when that helps explain the change.
- `fallback` records an actual recovery, omission, simplification or exhausted budget. It is not necessarily a generation failure.
- `error` identifies a failed attempt; the service adds `generation.failure`. The complete outcome also carries the stage, error code and detail independently of the bounded trace.
- `subject` is a zero-based local colony/feature index or `null` for an aggregate. Its meaning is specific to the key. A feature index is not automatically a team ID, especially before `dealStarts`.

Records are ordered and keys may repeat, for example when the crop guarantee runs again after clearing. Preserve sequence and subject during analysis. Do not collapse records into a dictionary and silently retain only the last colony or repair pass. A missing record means the operation did not emit an observation; it does **not** mean zero. Negative values such as an unreachable distance retain their internal sentinel meaning; unlike final-map report distances, they are not automatically converted to `null`.

Use a stable generator/helper namespace and a descriptive operation name. Include units or the precise stage in the name where needed: water corners versus pure-water tiles, desired versus placed features, crop placements versus distinct new deposits, cutting cost versus walk steps. Keep a key's type and meaning stable; rename it when its meaning changes. Numeric and choice keys are intentionally generator-specific, not a mandatory identical list for every map. The trace envelope has its own `schema_version` (currently 1); retain the report version, generator revision, request and source commit with bulk results.

## What is instrumented

Every built-in generator emits its own observations. Examples include City states' effective home/heart kinds and islet counts; Canals' block-kind histogram and wall/moat fallbacks; Anthill's chambers and room shortfalls; the height-field modes' fitted parameters; Watershed's start-search relaxation; Braided river's clamped channel count, crossings found and riffles by kind (forced, tree, loop), and its short-stretch and relaxed-home-distance fallbacks; and optional island, lake, farm and crossing shortfalls across the catalog. Uniform reports its intentionally empty starting-colony configuration.

Shared helpers report reusable decisions:

| Helper | Observations |
| --- | --- |
| `Resources` | Scatter targets; before-topup crop distances; whether a topup is needed; attempted successful placements; close/far search choices; wall clearing; cramped-start room before/after relief. |
| `BalancedStarts` | Candidate and shortlist counts, viable sets, selected resource-cost spread and failed searches. |
| `Terrain` | Height-field thresholds/tiling, start-search fallback and grove shortfalls. |
| `Settlements` | Candidate and worker-room constraints, relocation and placement. |
| `Towers` | Planned sites, route-blocking removals and equalized counts. |
| `Pipeline` | Whether non-default abundance triggered cramped-start relief. |

These are deliberately selective observations, not traces of every rejected candidate. Some helpers have no context and expose results to their generator caller instead. When a new design needs an explanation that is missing, add it at the actual decision or return-value site rather than reconstructing the algorithm after the fact. Search the emitting code for a key's exact current meaning.

## CLI success and failure reports

```sh
build/src/glob2 --generate-map city-states --seed 7 --width 256 --height 256 --teams 4 --json artifacts/city-7.json
```

Report schema version 2 adds `report_type`. A successful generated or loaded snapshot uses `"map"`. A failed service attempt writes `"generation_failure"` with `engine` and `generation` only, and the CLI still exits nonzero. It does not analyze or publish the partial map. Existing map/PNG outputs are not replaced by a failed attempt. Argument parsing errors, missing input/config files and invalid scalar CLI values occur before generation and do not promise a JSON report; keep stderr and the exit code too.

`generation.outcome` provides `success`, `stage`, `error` and `detail`; `selection_quality` is `null` on failure. `generation.raw_request` preserves raw width/height exponents and the options map, including malformed or unknown options in service-level reports. Resolved `parameters` is `null` when controls cannot safely be interpreted, and `generator` can be `null` for an unknown numeric method. Neither an invalid request nor a telemetry failure should be hidden by trying to analyze an unfinished world. See [REPORT.md](REPORT.md) and [the schema](map-report.schema.json).

## Bulk collection and ad-hoc analysis

Use the same production CLI on a bounded seed/settings matrix; use a fresh output directory so files from an older run cannot masquerade as success:

```sh
python3 tools/map_telemetry.py collect --generators canals,city-states --seed-start 1 --count 8 --set width=128 --set height=128 --set teams=4 --jobs 3 --out artifacts/telemetry-port
python3 tools/map_telemetry.py summarize artifacts/telemetry-port
```

The tool retains individual reports, process diagnostics, a manifest and flattened observations. It uses disposable profiles. Inspect failures and incomplete traces before comparing successful maps. Use the raw JSON for specific hypotheses; the tool's summaries are starting points, not acceptance thresholds.

A useful investigation asks: “Does increasing canal width make farm plots disappear, and does that reduce reachable building room?” Group by generator, revision and complete settings, count **maps** with the omission divided by attempted maps, then inspect affected subjects and their resource/room outcomes. Do not divide by number of event records: one map can produce the same fallback many times. Missing observations or dropped records require an explicit incomplete category. Compare requested/actual counts, variant frequencies and worst-colony distributions, including failed seeds. Keep seed-level rows so a surprising aggregate leads back to a reproducible map.

Tune on one seed range and confirm on another, including parameter extremes and rectangles. Stratify by variant: a rare fallback may be invisible in a global average. Join internal observations with the existing final-map measurements and retained previews to test the causal explanation; telemetry alone does not prove playability or fairness. Follow up with AI games and human play as described in [the game-rules guide](GAME_RULES_FOR_MAP_DESIGN.md).

## Distributed bulk studies

For multi-host or interruption-tolerant runs, use the shared
[distributed study workflow](../../.agents/skills/glob2-map-design/references/distributed-telemetry.md).
Structured `--generate-map --output-dir DIR` jobs always collect telemetry and embed
the complete native version-2 report in `result.json` under `map_report`, including
service failures. No map file is required. Native `--generate-map NAME --json FILE`
remains available unchanged. Offline reanalysis preserves raw records and final-map
numbers and reports map-weighted summaries, missing/incomplete traces and observed
fallback/variant rates. Candidate sampling retains only the chosen attempt trace.

## Permanent metrics versus temporary debugging

Keep a permanent metric when it explains a meaningful design outcome across many seeds or exposes a regression: effective variant, intended/actual optional feature count, fit budget, calibration input/output, skipped plot, repaired route, or collapsed feature. Give it a stable key/type, clear subject and bounded cardinality. Include related measurements so a fallback is actionable rather than an unexplained alarm.

Temporary debug instrumentation is useful for one hypothesis: every rejected point coordinate, flood queue contents, per-tile fields, intermediate image dumps, pointer values or verbose iteration traces. Keep it outside the permanent API stream (or in a short-lived local patch), disabled during normal benchmarks, and remove it before the final change. Do not commit an always-on debug stream simply because the collector has a cap. Promote only the small aggregate or decision observation that will remain useful, document its meaning, and test its limits. Timing belongs in external harnesses, not in deterministic generator decisions.

## Verification

`MapGeneratorDefaultsTest` exercises typed values, bounds, disabled collection and partial failure traces. `test/test_map_report.py` verifies JSON types/escaping, failure output, malformed request serialization, schema rejection, no map-byte changes when JSON is requested, and repeatability. `MapGeneratorGoldenTest <profile> --telemetry` compares enabled/disabled full serialized worlds and RNG state for all generators, checks repeatable traces, and prints separate generation timings. Timing differences are reported rather than asserted against a flaky wall-clock threshold. Existing golden rows still protect pre-change maps; telemetry-only changes should not require generator revision bumps.

## Fractal generator records

`fractal.homes.*` reports bounded-search attempts, candidate/fitted counts, optional spare modules,
and the site omitted after finished-world scoring. Search relaxation is always zero;
Hilbert emits a search batch for every attempted uniform order. `fractal.home.*` subjects are stable
home-site indices, not team indices; `home.team` records the later random deal. Frontages
count resource-adjacent walking edges (a deposit may have several); renewable frontages
also require positive exact engine growth probability. Expansion anchors overlap and are
scoring inputs; final validation separately checks a nonoverlapping building arrangement.
Contact records repeat by source-site subject, with targets in site-index order. Walking
and swimming use the engine's corresponding hard-space predicates.

`fractal.crossing.*` subjects are candidate IDs; benefits are graph-distance reductions
at selection time. `fractal.crossings.*` counts are per selector invocation: Hilbert emits
one mandatory batch followed by an optional batch. Sum counts across batches within a
map; do not treat their mean as the map's bridge count. Gardens counts opposing pairs.
`local-shortfall` and `major-shortfall` are unspent optional budgets, not silently omitted
mandatory connectivity. `sierpinski.depth.*`, `sierpinski.regions.*`, `hilbert.depth.*`, and
`hilbert.orientation` explain achieved geometry. `*.bank-farms.proposed/placed` exposes
actual farm omissions. No telemetry-only scans or random draws are used.
