# Hedgerow Country: full-parameter generation sweep

Screenshots, sample maps/replays, complete bulk request/metric rows, and test logs
are available in the [review evidence](../artifacts/hedgerow-country/README.md) and the [evidence branch](https://github.com/Globulation2/glob2/blob/evidence/hedgerow-country/docs/artifacts/hedgerow-country/README.md).

This checks generation reliability and extreme layouts, first on revision 5
and then on revision 6 after correcting a rare fertile-hedge construction failure.
The repair adds reusable tessellation operations and changes the behavior of this
optional generator. Simulation and AI behavior are unchanged. The evidence is in
`artifacts/hedgerow-bulk/`; its `plan.py`, `plan.json`, `analyze.py` and committed
`results/` make the sampling and screening reproducible.

## Final result

| Cohort | Supported maps generated | Unsupported requests correctly rejected |
| --- | ---: | ---: |
| Revision 5 baseline | 1,585 / 1,586 (99.94%) | 133 / 133 |
| Revision 6, shared primitives | **2,098 / 2,098 (100%)** | **133 / 133** |

All **512 fresh seeds passed**, including 256 extra compact-field/deep-hedge
stress cases. No final-build crashes, timeouts, generation failures, disconnected
starts, missing starter-resource access, cramped-start flags, or unhealthy telemetry
were observed. This is finite coverage of the control domain, not a guarantee for
every possible seed and parameter combination.

The shared safeguard activated on **12 maps (0.57%)**, all with compact fields
and maximum hedge depth. Every case needed **one contraction**; none needed more
or fell back to the regular lattice. Ten of these were fresh seeds. Exact requests
are in `shared/contractions.json`.

Final-build generation time was median **0.331 s**, 95th percentile **2.053 s**,
and maximum **3.021 s**; peak RSS was at most **113.3 MiB**. Every colony had at
least **395** overlapping 4×4 placement origins. Greatest nearest-wheat and
nearest-wood distances were **16** and **19** walking steps. These timing cohorts
have different request mixes and are not a controlled speed comparison.

**Six maps (0.29%) still have strongly uneven expansion territory**, all already
flagged in the baseline. The worst ratio remains 0.135 (about 7.4:1). None of the
fresh maps triggered the predeclared territory warning. Connectivity and opening
resources pass, but competitive fairness across all settings is not established.
The exact outliers and their interpretation are documented below.

## Coverage

| Block | Cases | Purpose |
| --- | ---: | --- |
| Supported geometry / worker endpoints | 346 | Every legal size, field-size and colony-count combination, with one and eight workers |
| Layout cross-product | 216 | Every combination of field size, hedge thickness, gateways and wooded share |
| Mixed parameter samples | 1,024 | Fixed sampling seed 5102026; independent resource mixes and synchronized scarcity/crowding |
| Unsupported geometry | 133 | Expected rejection of undersized fields or more colonies than fields |

The baseline supported-case denominator is **1,586**; the final cohort adds
512 fresh cases for **2,098**. Expected rejections are counted separately. Width/height choices cover 64, 128, 256 and 512 tiles, including both
rectangle orientations. A 64-tile side is deliberately unsupported for this map.
Colony counts span 1–12 wherever the field budget permits them; worker counts
span 1–8. All three field sizes, all hedge/gateway choices, all six wooded-share
levels and all thirteen levels (0–300%, step 25) of each resource slider appear.
This covers the full marginal domain and the layout cross-product, while sampling
the much larger combined resource domain. It is not exhaustive over every seed
and parameter combination.

The immutable Linux x86-64 revision 5 bundle from the fairness playtest was reused
and checked against the current generator source. The shared tournament coordinator
ran on `devlaptop.local` with four local worker instances and eight total execution
slots. Keeping coordination on that host avoids per-chunk SSH overhead. Jobs use
no best-of-seed selection, a 60-second timeout, and a one-hour infrastructure lease.
All accepted and late-attempt evidence is preserved by the shared framework.

Two additional macOS ARM64 probes exercise seed 0 on 128×128 with one worker per
colony, and seed 4,294,967,295 on 512×512 with twelve colonies, eight workers,
maximum hedges/resources and no extra gateways. They are separate from the Linux
sample and do not establish cross-platform simulation equivalence. Both were
repeated successfully on the final revision 6 build (`seed-zero-r6.*` and
`seed-max-r6.*`).

## Checks and interpretation

The production validator checks dry structural hedges, intact open lanes, access
to every field and connected colonies after settlement and resource placement.
The offline screen also checks requested colony/worker counts, unknown tiles,
missing or invalid telemetry, and actual worker access to starter wheat and wood.

Before running, the study set these outlier thresholds: generation over 10 seconds,
peak RSS over 512 MiB, fewer than 64 reachable 4×4 placement origins, starter crops
more than 32 walking steps away, or weakest/strongest exclusive walking territory
below 0.20. Placement origins overlap; they are not independent buildings.
Territory is ground reached sooner by a colony's workers than by every rival.
A low territory ratio is a fairness warning, not evidence of disconnected terrain.

See `outliers.json` for exact flagged seeds, requests and measurements. Native
previews and JSON reports of inspected outliers are retained beside it. These
initial-map checks cannot certify human balance, late-game traffic or whether a
particular hedge shortcut is worth the harvesting time.

## Rare failure and correction

Revision 5 seed **1012498260**, 256×256, five colonies, seven workers,
field size 64, hedge thickness 4, gateways 50, wooded share 80, and resource
amounts wheat/wood 75 and stone/fruit 150 failed final validation. A diagnostic
reproduction identified fertile structural wood at **(179,140)**. Circular
centre clearance had missed a diagonal boundary inside the square growth probe.

Revision 6 checks rasterized, dilated potential boundaries against each pond's
full possible growth envelope before assigning gateways and wood. An unsafe
warp has its corner offsets halved toward the regular lattice, with a bounded
lattice fallback. This conservative check includes boundaries that will later be
open or bare, so changing wooded share cannot reintroduce the defect. It preserves
hedge depth and uses no additional RNG draws. Final exact engine fertility and
connectivity validation still run after furnishing the map. Telemetry records
`hedgerow.warp-contractions`; the retained regression also checks that collecting
telemetry leaves the finished map unchanged.

Verification uses every original request plus **512 fresh seeds** (sampling seed
6102026): 256 emphasize compact fields and maximum hedge depth, and 256 resample
the supported domain. The new cohort has **2,098 supported requests and 133
negative controls**. Its immutable source snapshot, Linux bundle, plan, reports
and results are retained under `artifacts/hedgerow-bulk/`, with the new result tree
in `shared/results/`. `plan-shared.py` declares this cohort before dispatch.
The short-lived `revision6/results/` cohort used an inline implementation of the
same repair; it was cancelled when the operation moved into the shared framework.
Its attempts are retained separately and do not enter the final sample count.

## Reusable implementation

The new tessellation toolkit operations (free functions in `MapGeneration`)
are `rasterizeBoundaries` and `relaxWarpOutside`. The first creates sealed, thick,
toroidally wrapped masks from selected edges. The second contracts corner offsets
until those boundaries avoid an arbitrary exclusion mask, with no random draws
and a fixed retry bound. It returns the contraction count or failure if the
reference geometry also collides. It does not claim to validate polygon topology
or every other clearance constraint; callers still validate their complete maps.

Hedgerow Country now supplies only its pond growth envelope and thickness policy.
It also uses the shared rasterization operation to construct the final hedges.
Focused toolkit tests cover seam thickness, empty selections, safe no-op behavior,
repeatable repair, explicit fallback and impossible exclusions. Existing callers
of `warpCorners` keep their previous behavior.

The final source is reconstructible by extracting `revision6-source.tar.gz`,
then `shared-source-overlay.tar.gz`. `SOURCE_PARENT.sha256` in the overlay links
it to the retained base; the overlay's SHA-256 is the final bundle's dirty source
identity. The Linux immutable bundle is
`e9e7c1b0101b7a399f836bc9adc6a889c6488187ce1805123dd186c3be080429`.

## Completed baseline

Revision 5 completed all **1,719** logical jobs: **1,585/1,586 supported requests
succeeded (99.94%)**, and **133/133 unsupported requests were correctly rejected**.
There were no crashes, timeouts, unknown tiles, disconnected colonies, distant or
unreachable starter crops, insufficient-room flags, or unhealthy telemetry among
the successful maps. The sole construction failure is documented above.

Generation time was median **0.371 s**, 95th percentile **2.073 s**, maximum
**3.103 s**. Peak process RSS was at most **113.9 MiB**. Every successful colony
had at least **395 overlapping 4×4 building placement origins**; the greatest
reported nearest-wheat and nearest-wood distances were **16** and **19** steps.

Six successful maps (**0.38%**) had weakest/strongest exclusive walking territory
below 0.20: seeds 510015, 510042, 510043, 510294, 510297 and 2909909006.
The minimum ratio was **0.135** (about **7.4:1**), on seed 510294. Native previews
of 510015, 510294 and 2909909006 were inspected and retained. Their starter farms,
building space and roads work, but some colonies are much closer to vacant fields.
Most warnings used dense hedges and no extra gateways; 2909909006 also shows that
narrow rectangular maps with fewer wooded boundaries can have uneven expansion.
These are substantial balance warnings, even though the maps pass structural
validation. The sweep does not certify every supported setting as competitively fair.

## Compatibility and continuity checks

The shared refactor produces a **byte-identical saved map** to the initial inline
repair for seed 1012498260 (`shared-refactor-byte-comparison.txt`). The Linux save
also loads and renders on macOS (`repaired-loaded-macos.*`); this is save-file
portability evidence, not a cross-platform per-tick simulation comparison.
All eight stored Hedgerow default fingerprints remain unchanged from revision 5;
only their generator revision changes to 6. The previous AI-game evidence concerns
those earlier layouts and does not independently validate newly contracted warps.

The full `MapGeneratorDefaultsTest` passes on both Linux x86-64 and macOS ARM64,
including the new shared-primitive fixtures and retained map failure. The final
macOS `MapGeneratorGoldenTest --require-rows` compares **256 rows with zero
failures**. `git diff --check` is clean. Logs are retained as
`shared-linux-defaults.log`, `shared-macos-defaults.log`, and
`shared-macos-golden.log`. No Windows run or cross-platform simulation checksum
comparison was performed; the new operations only affect callers choosing them.

The conservative safeguard also contracts seed **510046** (128×512, twelve
colonies, one worker, field size 64, thickness 4, no extra gateways, all boundaries
wooded, resources 100). The previous map passed finished-world validation. The new conservative check
protects potential boundaries before opening gates, ignores mirrored-sand blocking,
and includes one tile of envelope slack, so it can also alter an already-valid map. Its inspected preview remains connected and retains thick hedges.
Minimum building room changes from 626 to 697 origins, maximum nearest wood from
13 to 11 steps, and territory ratio from 0.373 to 0.357. This illustrates why the
clearance repair is a reliability measure, not a promise of improved balance.
`contracted-510046.*` retains the request, map, preview and native report.

The paired rerun preserves the recorded room, crop-distance and territory metrics
for **1,584 of the 1,585 previously successful maps**. The other successful map
is the conservative contraction case above; the former failure now succeeds.
`paired-verification.json` records the comparison and its scope. This is an
initial-map metric comparison, not a byte-identity claim for every generated map.

## Reproducing the evidence checks

From the repository root:

```sh
PYTHONPATH=. python3 artifacts/hedgerow-bulk/analyze.py artifacts/hedgerow-bulk/shared
PYTHONPATH=. python3 artifacts/hedgerow-bulk/verify-artifacts.py
```

The immutable `plan.json` files can be resubmitted with their retained Linux bundles
through the shared tournament framework; `plan.py` describes the original sampling
and requires the archived revision 5 source, while `plan-shared.py` constructs the
final paired/fresh cohort. Use a new result directory for a new execution. Full
native reports, all attempt records, stdout/stderr and artifact hashes are retained.
`artifact-verification.json` records verification of the downloaded artifacts.
Workers are stopped after all their work is acknowledged; each cohort retains a
`workers-stopped.json` audit. The cancelled inline experiment is retained separately.

The downloaded evidence passed **11,905 artifact-file hash checks** across the
baseline, final and cancelled intermediate cohorts. The two complete cohorts have
one attempt per logical job (1,719 and 2,231 respectively). The cancelled inline
cohort retains 173 accepted results and 213 attempt records; it is excluded from
both reported success rates. All twelve worker instances across the three cohorts
were stopped after their queues had no unacknowledged work.

A final native preview of fresh seed **4132429259**, a contracted 512×512 layout,
was also inspected: its farm plots, thick continuous hedges and intended openings
remain intact (`contracted-fresh-4132429259.*`).
