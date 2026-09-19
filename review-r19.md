# Candidate 19 bounded review

No blocking correctness defect found in the court-selection, home-facing access, rim planting, or budget-preserving re-sowing changes. The narrow-map opening failure is resolved in the four supplied candidate 18 rotations. This supports retaining the generator-only fix without changing any AI. It does not establish uniformly sustainable late economies on narrow maps.

Reviewed frozen `candidate19-source.cpp` against `candidate17-source.cpp`, the shared `plantContainedPlot` implementation, all four `r18-narrow-17-r*` logs/results, `regressions-r19.jsonl`, and the initial/final PNG pairs described below. No build, new simulation, or source edits were performed. The candidate 19 bulk studies were still running when this review was written.

## Source findings

- **Court selection:** Only long compact starter plots receive `serviceHome`. The maximum toroidal distance of all four court corners to home is minimized first, then fertile rim quality breaks ties. Tiny 64² and ordinary full-map court selection retain their previous ordering. The productive-growth floor becomes six for long compact maps, while 64² retains two. The radius change is similarly confined to long compact wheat.
- **Access:** Multi-source cardinal BFS still gives a shortest path from the court to the selected boundary. For home-facing courts it examines the whole plot and chooses the boundary tile nearest home. It cannot escape the plot or corrupt its predecessor chain: all initial court cells have parent -1, and new cells are inserted only once. The existing materialized worker-component check remains necessary because geometric proximity alone does not establish external accessibility.
- **Rim planting:** Court and access cells remain excluded. Rim-first planting consumes the existing requested budget, and the second call cannot double-count occupied rim tiles because `plantContainedPlot` filters for clear ground. Fertility eligibility is retained.
- **Fallback:** It triggers only when the actual planted wheat cannot service the court. Clearing is restricted to wheat on this plot's plantable tiles. The replacement budget is the previous actual count, not the requested count, so this cannot add an abundance exemption. It uses the same eligible set and deterministic ranking, then the ordinary service-site and minimum-resource checks still reject an unsuccessful retry. Existing successful full/tiny layouts skip this branch.
- **Cost:** Extra BFS work is bounded by the small plot; re-sowing performs small local scans/sorts. There is no new whole-map flood or simulation loop. No plausible significant performance regression appears in these changes, though the ongoing study is the appropriate measurement.

Two nonblocking clarity improvements remain: the rim extraction is duplicated, and the planting comment describes discovery as assured although the code only makes it more likely. Workers still need to visit enough of the rim to reveal both grown-footprint corners. A shared helper and wording such as “encourage food haulers to discover” would be clearer, but neither justifies delaying validation or changing geometry.

The three former failure requests all succeed in `regressions-r19.jsonl`. Seeds 100726 and 100793 exercise long compact changes; 100908 exercises the ordinary full-map re-sowing fallback and records `farm.resown`. The latter still selects landscape 10, so this is a successful regression, not evidence that every rare search difficulty has disappeared.

## Narrow-map playability evidence

All four rotations run the same candidate 18 narrow seed 17 map for 30,000 ticks. Cortex finds an inn site by tick 512–1024 and has an inn by tick 1024–2048. All 16 colony/AI combinations have zero worker starvation through 15,000 ticks. Every combination harvests wood and develops buildings; the previous opening deadlock is absent.

The table reports physical start order 0–3, not a reordered AI ranking. Births, starvation and combat columns count workers only.

| Rotation | AI order | Worker births | Worker starvation | Workers killed in combat |
|---|---|---|---|---|
| 0 | Nicowar, Cortex, Cabino, Maxima | 28, 4, 28, 34 | 0, 0, 0, 6 | 2, 0, 2, 16 |
| 1 | Cortex, Cabino, Maxima, Nicowar | 8, 16, 28, 26 | 6, 3, 0, 5 | 0, 1, 0, 11 |
| 2 | Cabino, Maxima, Nicowar, Cortex | 24, 29, 33, 1 | 0, 1, 0, 0 | 4, 0, 1, 3 |
| 3 | Maxima, Nicowar, Cortex, Cabino | 31, 24, 19, 29 | 28, 17, 0, 0 | 3, 8, 0, 0 |

The formerly failing Cortex at physical start 2 now harvests 170 wood, produces 19 workers, has no worker starvation, and ends with 85 total units and 17 buildings. This is direct evidence that home-facing courts fix the observed failure rather than merely increasing resource counts.

There is a remaining late-economy limitation: rotation 3 Maxima at start 0 loses 28 workers and 18 warriors to starvation and is marked eliminated at tick 27,361. Nicowar at start 1 loses 17 workers to starvation. Combat occurred as well, but Maxima's starvation exceeds its combat losses substantially; do not describe that outcome as purely military. Cortex is also slow at other starts (one to eight worker births), despite successfully constructing its first inn. The study demonstrates functional openings and meaningful development, not equal AI strength or starvation-free indefinite growth.

**Recommendation:** Accept this as a narrowly scoped opening/playability improvement without tuning the AI engine. Report the late narrow-map food pressure honestly. Any stronger claim that all long compact colonies sustain aggressive late growth would require additional food/service-capacity evidence or further generator work; it is not supported by these four rotations alone. They also cover one landscape seed, not the full narrow-map envelope.

## Visual assessment

Viewed `release-full-1.png` / `release-full-1-final.png` and `r18-narrow-17.png` / `r18-narrow-17-final.png`.

The full map has legible bent shorelines, varied opposing bays, broad wooded ridges and distinct shoreline farms. Crop growth remains inside the intended boundaries in the final preview; the lake and route structure stays readable. The earlier capsule repetition and circular crop disks are substantially reduced. Roads still contain long straight segments, and small isolated resource pools read as repeated icons, but neither is a new candidate 19 regression or a reason for another broad redesign.

The narrow map necessarily reads as a chain of shoreline settlements with some wrapping across the left/right seam. Enlarged fields occupy more shoreline but remain irregular, and the final image retains open connections and recognizable lakes. This is a coherent compact interpretation of the design, with less neutral space and less sweeping lake interplay than the full version. These terrain previews show landscape and resource evolution; they do not replace an in-game view for judging building crowding or action readability.

## Final disposition

No new code blocker identified. Keep the scoped generator changes; finish and retain candidate 19 generation/control/envelope evidence, including the crowded all-low corners. Describe the narrow rotations as successful opening regressions with a documented late-food-pressure limitation. No AI or core simulation change is warranted by this review.
