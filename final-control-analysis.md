# Candidate 19 final control analysis

Independently verified all 777 candidate 19 control rows and all 436 envelope rows: every row reports successful generation and exit code zero. All 1,213 request keys match the candidate 17 studies exactly, with no missing, duplicate or extra requests. The original control-analysis.md/json are retained unchanged; this report is a new comparison, not a relabeling of old evidence.

Inputs: `controls-r19/ablation.jsonl` versus `controls-final/ablation.jsonl`; `envelope-r19.jsonl` versus `envelope-final.jsonl`. Pairing uses study, control/value, dimensions, colony count, seed and the complete option dictionary. Timings are excluded from geometry comparisons; the newly added `farm.resown` key is counted separately. Metric equality is not a proof of byte-identical maps.

## What changed

| Dataset | Requests | Failures | Changed metric rows | Unchanged metric rows |
|---|---:|---:|---:|---:|
| Controls | 777 | 0 | 9 | 768 |
| Envelope | 436 | 0 | 72 | 364 |

All 17 default control baselines and every control family other than Wheat amount retain identical recorded metrics. The nine changed control rows are low-wheat requests. All 68 long compact envelope rows change, as expected from the home-facing court and larger productive field. Four additional combined all-low envelope requests change: 64²/2 colonies, 128²/4, 128×512/12 and 256×512/12, each seed 97.

Re-sowing can rescue a previously rejected scored proposal. Consequently, a previously successful request can now choose a different proposal even if its final chosen layout did not need re-sowing. This explains the changed seed 7 wheat25/wheat75 rows without final `farm.resown` telemetry; do not claim that every previously successful request retains its map. Default baselines do retain all measured metrics.

## Crowded all-low corners

| Request, seed 97, all controls at minimum | Candidate 17 landscape index | Candidate 19 landscape index |
|---|---:|---:|
| 64×512, 8 colonies | 21 | 0 |
| 512×64, 8 colonies | 22 | 0 |

Both formerly near-exhausted requests now succeed on the first landscape. These are exact paired requests. Across all envelope rows the highest selected landscape index falls from22 to5; controls fall from2 to2, with fewer retries overall. The narrow planting/court changes resolve the specific retry-margin concern raised in the previous analysis. This does not prove a universal success guarantee for unseen seeds.

## Knob effects rechecked

| Control / output | Paired transitions | Flat transitions | Decreasing transitions |
|---|---:|---:|---:|
| lake-elongation: terrain%:water | 74 | 0 | 74 |
| lake-elongation: tel:portage-lakes.swim.final-saving | 74 | 4 | 8 |
| portage-depth: tel:portage-lakes.portage.wood-tiles | 66 | 0 | 0 |
| extra-trails: tel:portage-lakes.trails.placed | 50 | 17 | 0 |
| wheat-amount: tiles:wheat | 114 | 0 | 0 |
| wood-amount: tiles:wood | 114 | 0 | 1 |
| wood-amount: tel:portage-lakes.timber.planted | 114 | 0 | 0 |
| stone-amount: tiles:stone | 114 | 24 | 0 |
| algae-amount: tiles:algae | 114 | 19 | 0 |
| fruit-amount: tiles:fruit | 114 | 1 | 0 |

Direct wheat, renewable timber, stone, algae and fruit output remains nondecreasing across every paired transition. The previous findings remain valid:

- Elongation reduces pure-water coverage at every tested step, while swimming savings are not monotonic for individual seeds.
- Portage depth gives exactly three cuttable wood tiles per requested row.
- Extra trails remains effective and nondecreasing, with feasible-route saturation; all 160 one/two-colony trail study rows retain identical metrics.
- Algae250=275=300 for all eight default-map seeds. Stone50=75,150=175,250=275 for all eight seeds. These documented capacity/whole-tile plateaus remain.
- Renewable timber strictly increases. Total wood has the same single decrease at256²/4 colonies/seed4, wood275→300, caused by selecting a different settlement proposal.

The tables below give current eight-seed default-map means, including the matching default setting. Full per-seed transitions and changed metrics are in final-control-analysis.json.

### lake-elongation

| Setting | terrain%:water | tel:portage-lakes.swim.final-saving |
|---|---:|---:|
| 125 | 11.37 | 50.50 |
| 150 | 10.56 | 54.38 |
| 175 | 10.00 | 59.38 |
| 200 | 9.63 | 61.12 |
| 225 | 9.33 | 62.62 |
| 250 | 9.08 | 65.88 |
| 275 | 8.91 | 71.12 |
| 300 | 8.75 | 74.62 |

### portage-depth

| Setting | tel:portage-lakes.portage.wood-tiles |
|---|---:|
| 2 | 6.00 |
| 3 | 9.00 |
| 4 | 12.00 |
| 5 | 15.00 |
| 6 | 18.00 |
| 7 | 21.00 |
| 8 | 24.00 |

### extra-trails

| Setting | tel:portage-lakes.trails.placed |
|---|---:|
| 0 | 3.00 |
| 25 | 3.25 |
| 50 | 4.00 |
| 75 | 4.75 |
| 100 | 6.00 |

### wheat-amount

| Setting | tiles:wheat |
|---|---:|
| 0 | 128.00 |
| 25 | 256.00 |
| 50 | 384.00 |
| 75 | 512.00 |
| 100 | 640.00 |
| 125 | 768.00 |
| 150 | 895.12 |
| 175 | 1021.12 |
| 200 | 1146.62 |
| 225 | 1268.88 |
| 250 | 1389.25 |
| 275 | 1507.00 |
| 300 | 1620.00 |

### wood-amount

| Setting | tiles:wood | tel:portage-lakes.timber.planted |
|---|---:|---:|
| 0 | 17440.75 | 6.00 |
| 25 | 17508.12 | 8.77 |
| 50 | 17530.25 | 11.53 |
| 75 | 17551.88 | 14.23 |
| 100 | 17573.25 | 16.91 |
| 125 | 17594.62 | 19.58 |
| 150 | 17615.50 | 22.19 |
| 175 | 17635.50 | 24.69 |
| 200 | 17655.00 | 27.12 |
| 225 | 17779.25 | 29.53 |
| 250 | 17797.62 | 31.83 |
| 275 | 17815.62 | 34.08 |
| 300 | 17727.50 | 36.19 |

### stone-amount

| Setting | tiles:stone |
|---|---:|
| 0 | 802.12 |
| 25 | 805.88 |
| 50 | 809.62 |
| 75 | 809.62 |
| 100 | 813.25 |
| 125 | 816.88 |
| 150 | 820.25 |
| 175 | 820.25 |
| 200 | 823.50 |
| 225 | 826.75 |
| 250 | 830.00 |
| 275 | 830.00 |
| 300 | 833.00 |

### algae-amount

| Setting | tiles:algae |
|---|---:|
| 0 | 32.00 |
| 25 | 40.00 |
| 50 | 48.00 |
| 75 | 56.00 |
| 100 | 64.00 |
| 125 | 72.00 |
| 150 | 80.00 |
| 175 | 88.00 |
| 200 | 96.00 |
| 225 | 103.62 |
| 250 | 104.75 |
| 275 | 104.75 |
| 300 | 104.75 |

### fruit-amount

| Setting | tiles:fruit |
|---|---:|
| 0 | 0.00 |
| 25 | 10.00 |
| 50 | 16.00 |
| 75 | 20.00 |
| 100 | 26.00 |
| 125 | 36.00 |
| 150 | 42.00 |
| 175 | 46.00 |
| 200 | 52.00 |
| 225 | 60.62 |
| 250 | 66.50 |
| 275 | 70.25 |
| 300 | 76.12 |

## Re-sowing frequency

The telemetry key appears in 7/777 control requests (0.90%) and4/436 envelope requests (0.92%):11/1,213 overall (0.91%). Each recorded mean is1; this is a count of requests in which at least one selected plot was re-sown, not the number of plots or attempted proposals. JSONL aggregation averages repeated telemetry records and cannot recover those counts.

| Dataset | Configuration | Seed | Wheat amount |
|---|---|---:|---:|
| controls | 256×256, 4 colonies | 3 | 0 |
| controls | 256×256, 4 colonies | 6 | 0 |
| controls | 256×256, 4 colonies | 7 | 50 |
| controls | 256×256, 4 colonies | 8 | 0 |
| controls | 256×256, 4 colonies | 8 | 25 |
| controls | 128×128, 3 colonies | 2 | 0 |
| controls | 512×512, 6 colonies | 1 | 0 |
| envelope | 64×64, 2 colonies | 97 | 0 |
| envelope | 128×128, 4 colonies | 97 | 0 |
| envelope | 128×512, 12 colonies | 97 | 0 |
| envelope | 256×512, 12 colonies | 97 | 0 |

All observed re-sowing requests use wheat0–50. The fallback remains a rare low-budget repair in this sample and does not appear in a default baseline. No new broken control or unexpected saturation was found.

## Coverage and limits

Coverage is identical to the prior report: eight full default-map seeds across every individual control setting; extremes on three other size/count configurations; all16 ordered supported dimensions and every legal colony count at seed7/one worker and seed31/eight workers; the160 small-team trail runs; and32 all-low/all-high maximum-colony corners. These are generation/validation results, not a substitute for the narrow rotation playability evidence.

The separate candidate19 2,000-random-request study is still running and is intentionally excluded. This review did not independently rerun or inspect the reported cross-platform goldens/full suite. No source edits, builds or new game runs were performed.
