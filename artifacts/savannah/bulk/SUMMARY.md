# Savannah bulk generation audit

The final Savannah implementation generated **872/872 valid requests**: a 130-case
stress pilot and a separate 742-case fresh-seed bulk matrix. No seed was retried,
and every native report, request, process log and telemetry record was retained.
The job ran with four workers on the offered `pharaoh-dev-3.local` host (optimized
Linux x86_64 client); its observed load was 0.40 before dispatch.

## Coverage and result

The full matrix uses map seeds **23000–23741**. It includes:

| Group | Cases | Coverage |
| --- | ---: | --- |
| Terrain grid | 189 | All 21 dry values × 3 pond choices on 128×128/4, 256×128/8 and 128×256/8. |
| Resource ladder | 195 | All 13 legal values of each of five resource sliders, repeated on those three tight shapes. |
| Size seeds | 88 | Eight independent default seeds at 11 square/rectangular size and colony settings. |
| Shape edges | 30 | All-zero and all-maximum resources, worker extremes, on all 15 tested legal shapes. |
| Mixed | 240 | Deterministic independent mixed sliders, workers 1/4/8, and every size/colony shape. |

All supported sides from 128 to 512 and both 2:1 rectangle orientations were
represented. Square 128×128 maps used 1, 3 and 4 colonies; the crowded 256×128
and 128×256 rectangles used up to eight; larger maps used up to twelve. Requests
outside the documented area or spacing envelope are rejected by design and were
not counted as generation failures. The pilot used different seeds **11000–11129**
and targeted high pond/dry density, zero/maximum abundance and worker extremes.
`full/audit.json` verifies that every legal value of every typed control appeared.

The full run passed **742/742**, including every small and rectangular request.
Final-world checks therefore passed for retained pure-water ponds, contained
renewable crops, accessible wheat/wood, building space and both broad land routes.
Observed worst wheat and wood gathering distances were **14 walking steps** each;
the weakest home still had **745 reachable 4×4 placement origins**. That count
contains overlapping origins, so it is a room proxy rather than 745 independent
buildings. Start-quality fairness ranged down to 0.800, with a fifth percentile
of 0.849. These are static observations, not a balance guarantee.

## Optional features and abundance

The full requests targeted **4,092 neutral ponds** and placed **4,021**. There were
**55 maps with an optional pond omission** (7.4% of maps), and **zero maps without
the required neutral pond**. Sparse placed all 481 targets; Normal placed
2,348/2,360 targets and omitted a target on 12/436 maps; Many placed
1,192/1,251 targets and omitted a target on 43/154 maps. Most omissions were on
crowded small shapes:

| Shape / pond choice | Maps omitting target | Target ponds placed | Lowest pond count on a map |
| --- | ---: | ---: | ---: |
| 128×128 / 4, Normal | 6 / 97 | 188 / 194 | 1 |
| 128×128 / 4, Many | 15 / 27 | 63 / 81 | 1 |
| 256×128 / 8, Many | 14 / 26 | 138 / 156 | 4 |
| 128×256 / 8, Many | 14 / 26 | 133 / 156 | 3 |

Those counts reflect the existing radius-23 home reservations and five-tile
approach strips. Savannah's control describes a *target*; it keeps essential
starts and routes while optional ponds saturate when space is exhausted. No
home radius, minimum spacing, parameter range or pond heuristic was changed in
this round: no tested valid request failed, and the remaining omission cases are
contained optional saturation rather than a structural break. A future playtest
could revisit the Many density target if its actual frequency feels too sparse
on crowded 128-square maps.

High-abundance plots saturated on **264/742 maps**, as their contained fertile
space has a deliberate ceiling. No plot saturated on any of the **292 maps with
all five resource sliders at 100%**, or any of the 15 all-zero cases; all 15
all-maximum cases saturated at least one optional plot. Slider values above
ordinary abundance therefore have diminishing returns at the containment cap.
Starter wheat, wood and quarry floors still passed at zero abundance.

## Time, repeatability and limits

Process time, including native JSON report work under four concurrent jobs, had
median **0.37 s**, 95th percentile **2.55 s**, mean **0.67 s**, maximum **6.99 s**.
These are shared-host observations, not isolated benchmarks. Eight selected
requests were regenerated on the same Linux binary with a saved `.map`, preview
PNG and report. Terrain, resources, room, fertility, quality and movement report
sections matched their bulk originals exactly. The samples include the smallest
Many omission, a full small Normal map, both crowded rectangle orientations,
zero/maximum resources, the largest default map and the weakest static fairness
case. Their manifests record exact commands, and `samples.log` records 8/8
repeatability passes. Preview inspection shows open plains, separated plots and
neutral ponds with usable approaches on both rectangle orientations.

This audit tests generation and finished-map validation, not populated-game
balance. The earlier `artifacts/savannah/playtest-round1/` games remain the play
evidence. Bulk execution was Linux x86_64; the earlier optimized macOS arm64
build/defaults/golden/sweep checks remain separate. Windows and human play are
unverified. The matrix covers every control value individually and 240 mixed
combinations; it does not enumerate all 13⁵ resource combinations.

## Reproduction and artifacts

From the repository root after the optimized Linux build:

```sh
python3 artifacts/savannah/bulk/bulk_sweep.py /abs/new/output-pilot pilot 4
python3 artifacts/savannah/bulk/bulk_sweep.py /abs/new/output-full full 4
python3 artifacts/savannah/bulk/analyze_bulk.py /abs/new/output-full
python3 artifacts/savannah/bulk/make_samples.py /abs/new/output-full /abs/new/samples
```

`pilot/` and `full/` retain `requests.json`, `aggregate.json`, `audit.json`, native
`attempt-*/report.json`, manifests, stdout/stderr, flattened `records.jsonl` and
telemetry `summary.json`. `pilot.tar.gz` and `full.tar.gz` are portable copies of
the raw jobs. The latter has SHA-256
`beddd6ca7134a6fb5271293d056fb68f16c38e963bbf19394e5d7a86233fc990`,
verified against the host copy. `samples/` retains eight maps, previews, JSON
reports and exact request manifests. `review-sample-maps.zip` packages those eight
samples for download; the preview PNGs remain individually viewable in the PR.
The full audit and evidence are also included in the Savannah reviewer archive.
