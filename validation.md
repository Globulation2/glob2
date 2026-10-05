# Terrain refactor: local validation record

The implementation remains uncommitted on `codex/terrain-properties`, based on `934c3c588bea51ec7421152e15f5168b2aaf5502`. `final-provenance.json` records source fingerprints, final binaries, compiler/platform and fetched master. This is local review evidence, not an assertion of hosted CI or cross-platform full-game coverage.

## Implementation and independent review

Compile-time terrain IDs and gameplay/presentation tables drive canonical map state, movement and gradients, AI placement/tactics, resources and ecology, projectiles, health, JavaScript queries, rendering, editor, image/report tools and save/network boundaries. Ice and road have separate off-default authoring experiments; map requirements travel into matches and saves. Legacy save floor58 is retained, current format/replay floor134, network55, simulation revision14.

Independent reviewers covered core persistence/experiments/health/projectiles, movement and SIMD, AI/ecology/scripts, generators, and presentation/editor. They repaired findings and reran focused checks. Temporary audit narratives are under `docs/.work/terrain-*`; their early pending notes are superseded by the completed validation below.

## Final native checks

Release client and all test targets built with GCC15.2.0, Linux x86_64, -O3, SDL3 and recording SDK prefixes recorded in build logs. Tests use an isolated SDK copy containing the repository-required PNG16 preservation patch; original benchmark/game SDK libraries were hash-verified unchanged. See core-review dependency audit.

- `tests-restored-unit.log`: 792 unit cases passed; 17 display cases excluded from this headless pass and covered in earlier display runs.
- `tests-restored-final.log`: 290 focused test groups, 425 individual cases, all passed. Includes terrain, gradients, AI, JavaScript, map queries, save continuation, network simulation and map tiling, including terrain software/OpenGL rendering.
- Earlier integration failures were individually investigated and rerun; UI/editor/preview coverage is in `tests-ui-final`, generator/contract/fix coverage in `tests-fixes`, renderer and golden boundary coverage in `tests-final-boundaries`.
- External JavaScript original/realistic profiles: serial/parallel full per-tick traces, exact save/replay bytes, and six saved continuations passed (`javascript-final-v2`).
- CLI legacy initial saves and current traces: serial/parallel and v108 full reload checks passed (`cli-terrain-*`). Maxima v115 initial save/full trace/midpoint reload passed (`maxima-terrain-*`). Shared runtime 4096/6144 checkpoints continued with matching per-tick state.
- Map CLI, images and reports passed (71,64,16 commands respectively), including whole-cell material round trips and mixed terrain metrics. Generator template syntax compiled. Translation validation and simulation revision/golden checks passed. Python ecology analyzer8 cases and test-registry policy checks passed.
- `git diff --check` passed. Current master fetched; overlap is an already-applied upstream test correction and independent CI artifact list additions. No conflicting production changes or unnecessary rebase.

Production SHA256 `00a8d43494919ea78900d77214f93e75cf7516c8f6e7bceeb393120d715330ca` is byte-identical to the binary used by the primary performance matrix and full-game corpus. The final JavaScript braces-only cleanup does not change executable bytes.

## Independent algorithm checks

Sanitizer oracles cover mixed weighted ground gradients, injected cost aliases, air forward/reverse routing, strategic travel, closed-cell projectile tracing, coupled signed fertility fields and exact rational growth probability. Native scalar/SSE2 and ARM64/NEON under QEMU passed the applicable movement oracles. This is isolated kernel equivalence, not full ARM game determinism. Full-game Windows, macOS, browser and ARM comparisons remain unverified.

## Ecology and game feel

Training30maps and held-out17maps,16seeds per cohort, four horizons through65536ticks, untouched and replenished-harvest modes: every corpus resource error stayed below5%; all adequately sampled map/resource comparisons stayed within10%. Combined6495/7072 primary comparisons (91.84%) met baseline precision requirements. Some sparse resource cases remain statistically inconclusive; see `ecology/{train-report,holdout-report,cohort-summary}.json`. Holdout data was not used to tune coefficients.

All96 paired AI games completed (192 runs). Initial inputs and telemetry accounting passed integrity checks. Global supply and overall capped pacing remain close, but the common63-pair tick32768 cohort shows starvation765→1146 and hunger+1.01 percentage points. Several specific Maxima/Cortex games show insufficient feeding despite adequate crop supply. Later pooled rates do not establish a sustained overall rise, but they do not erase the midgame warning. Intervals are exploratory and not adjusted for multiple comparisons. See `ecology/full-games/analysis/assessment.md` for numerators, subgroup checks and limits; targeted diagnostic follow-up is retained alongside those games. Independent enumeration of all272 legacy sprites and seven ground movement profiles found identical classic passability, buildability and gradient costs. The initial Cortex diagnostic identifies stocked inns with every eating slot occupied and no eligible expansion site, not a measured food-stock shortage. Eight retained Cortex snapshots were inspected with an isolated diagnostic build: every tile had identical old/new buildability, no experimental terrain or movement modifiers were present, and current production placement matched the traced candidate rejection counts. At32768, all six empty sites with sufficient nearby wheat failed the unchanged adjacent-wheat rule;29 otherwise eligible locations were occupied by resources. The unchanged upgrade policy requires a spare inn. This establishes sensitivity of existing placement/upgrade rules to the changed settlement/resource trajectory, with no verified terrain classification defect. The matched Maxima canals rerun also reproduced the original trajectory: at24576,99 hungry targetless non-swimmers were on a water-separated component containing three completed swarms and zero completed inns, while inns elsewhere held104 wheat. Baseline had two inns on the corresponding component. Terrain-only eight-neighbor connectivity already rules out a walking route. This identifies colony feeding-infrastructure distribution as the immediate bottleneck, with no evidence of a new routing defect. Detailed source, commands and counts are retained in the follow-up artifacts. Human playtesting and broader balance/platform verification are still needed.

## Performance and limitations

Six scenarios ×7 alternating pairs (84 runs): median scenario CPU/tick change -8.80%, but allotments with growth is +18.89%. Setup is matched; RNG/AI trajectories change, so workloads differ. The classic property gradient specialization retains low overhead; generic weighted propagation and cache rebuilds are more expensive. Combined dense512² ecology rebuild measured roughly348ms; persistent growth cache12bytes/tile. Shared-host contention limits timing precision. The late constexpr queue metadata experiment regressed matched neutral games and was reverted; final binary exactly matches the primary matrix candidate. Full commands, hashes, samples and memory/rebuild results are in `performance/README.md` and adjacent artifacts.

Evidence is local and ignored. No PR, hosted verification, remote evidence upload, merge or deployment is claimed.
