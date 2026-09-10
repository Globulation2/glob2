# Refactor review

The control ranges and defaults from PR #238 are unchanged. The primary comparison uses 1,000 attempts per playable generator, seeds 20001–21000. Every generator's mean immediately buildable area stayed within 0.15 percentage points of the baseline. All default-generation start-proxy changes stayed within the three-point review threshold.

## Shattered Coast investigation

The primary cohort produced 26 placement failures rather than the baseline's 14: a 1.2-percentage-point increase, above the one-point review threshold. The failure was inability to space all colonies on grass, not an exception, timeout or incomplete world reported as successful. The original flag remains visible in `RESULTS.md`.

A separate 1,000-seed pilot (30001–31000) produced 33 failures. To investigate rather than retune around these seeds, the original PR #238 commit (`139e439f45f7cc4a616a968720549f9347b4a330`) was built in an isolated checkout. Both versions then ran 5,000 confirmation seeds, 30001–35000, with their compiled tuned presets:

| Measure | PR #238 | Modular generators | Difference |
|---|---:|---:|---:|
| Failures | 99 / 5,000 (1.98%) | 122 / 5,000 (2.44%) | +0.46 percentage points |
| All-team start proxy | 4,050 / 5,000 (81.0%) | 4,000 / 5,000 (80.0%) | −1.0 point |
| Mean immediately buildable area | 36.41% | 36.31% | −0.10 points |

The larger comparison did not reproduce the threshold crossing. In the first 1,000 confirmation seeds, the original implementation itself had 35 failures, compared with the refactor's 33, illustrating variation between finite seed cohorts. The algorithms and defaults were not changed to improve these results.

A two-sided Fisher exact test for the 5,000-seed failure comparison gives p=0.134. The approximate 95% interval for the failure-rate difference is −0.12 to +1.04 percentage points. This is not proof of identical distributions or a strict non-inferiority bound below one point. It supports accepting this structural refactor alongside the unchanged controls, close terrain measurements, code inspection and visual review, while retaining failure-rate monitoring for future generator changes.

The refactor intentionally changes seed-to-map outputs. This compares distributions, not matching historical map hashes. The baseline executable retains its historical fake-clock study harness; the new executable uses explicit seeds and no clock interposition.

[Confirmation statistics](confirmation.json) · [Baseline confirmation rows](baseline-confirmation.csv.gz) · [Refactor confirmation rows](modular-confirmation.csv.gz)

## Visual assessment

All three fixed seeds (22001–22003) and one weak successful start per generator were inspected against the earlier gallery. No fixed preview seed needed replacement.

| Generator | Preserved visual identity |
|---|---|
| Swamp | Broad, gently curved wetland channels and irregular connected grassy areas. |
| River | A dominant winding river, broad banks and occasional ponds. Existing straight bands at wrapping boundaries can still appear. |
| Islands | Rounded, separated landmasses with coastal resource belts and open interiors. |
| Crater Lakes | Connected grass punctured by round lakes, sometimes overlapping into larger pools. |
| Concrete Islands | Angular regions separated by narrow channels, with interior building space and larger resource patches. |
| Isles | Rounded islands joined by narrow land bridges, surrounded by more open water. |
| Shattered Coast | Fragmented, finely textured coastline and scattered inland building clearings. Weak seeds can remain predominantly water and fragmented grass. |
| Rugged Archipelago | Clearly separated islands with rough shores and chunky resource patches near the initial colonies. |

The weak examples remain useful diagnostics. A successful map can still have poor local expansion or resource access; the service's structural checks deliberately do not enforce the offline start proxy or repair the terrain.

## Edge settings and integration

The edge suite covers 208 configurations with four seeds each: individual control endpoints, all-minimum/all-maximum combinations, small crowded maps, rectangular maps and 512×512 maps with twelve colonies. The initial run completed 832 attempts with 717 successes and 115 clean failures, with no crashes or timeouts. Every configuration also passed independent repeated-process checks. Impossible all-zero terrain recipes fail request validation; difficult placement combinations may fail after partial generation, and their candidates are discarded.

The automated contracts exercise registered defaults/ranges/steps, both UIs' shared settings and mode memory, byte-codec roundtrips, legacy sentinels, a test-only nonconsecutive registration, immutable requests, interleaved map and full-state checksums, RNG restoration on success and failure, and structured errors. The custom-game harness exercises full save/load, real match launch, replay playback and native SDL interaction.

Raw rows, configuration manifests and reproducibility checks are retained alongside this report. `source-hashes.json` records the final generator and study sources used for the published gallery.

## Experimental-concept framework upgrades

The subsequent geometry, topology, discrete-control, validation and constrained-settlement upgrades are documented in [the concept review](../FRAMEWORK_UPGRADES.md). No experimental generator was imported. A repeat of all 8,000 default attempts matched the preceding modular implementation's map hashes and quality outcomes exactly. The new optional helpers are exercised by synthetic test registrations, and existing generators keep local dispersion and their original generation sequence.

The final edge rerun retained all 832 success/failure classifications (717 successes, 115 clean failures) and passed 624 repeated seed/configuration pairs. Fourteen hashes changed in small Concrete Islands or single-colony Isles configurations after the region-capacity and finite single-site-spacing fixes. [Framework regression counts](framework-regression.json).
