# Portage Lakes control and envelope analysis

The completed frozen studies passed all 1,213 generation requests: 777 control rows and 436 envelope rows. They show effective controls, documented resource saturation, and a crowded narrow-map retry weakness. These are generation checks, not evidence that every resulting AI economy succeeds. Candidate18 and subsequent planting fixes are newer than this evidence.

## Coverage

- Control study: every public non-default setting individually at 256×256, four colonies, seeds1–8; eight matching default baselines. This gives 616 ablations plus eight defaults. Each control’s two extremes also ran on 128×128/3 colonies, 512×512/6, and512×256/5, seeds1–3:144 extremes and nine defaults.
- Envelope: all16 ordered combinations of64,128,256,512 sides, every supported colony count (122 size/count configurations), each with seed7/one worker and seed31/eight workers:244 rows. Workers and seed change together, so this does not isolate worker-count effects.
- Additional envelope studies:160 trail settings (all five values, seeds1–8, one/two colonies,64² and256²);32 combined all-low/all-high settings at maximum colonies, covering every ordered size, seed97. This checks two corners of the parameter space, not every combination.

## Measured knob effects

Each table below uses the same eight default-map seeds. Values are mean actual output; resource counts include guarantees. Means are descriptive, not confidence intervals.

### lake-elongation

| Setting | terrain%:water | tel:portage-lakes.swim.final-saving |
|---|---:|---:|
| 125 | 11.37 | 50.50 |
| 150 | 10.56 | 54.38 |
| 175 | 10.00 | 59.38 |
| 200 (default) | 9.63 | 61.12 |
| 225 | 9.33 | 62.62 |
| 250 | 9.08 | 65.88 |
| 275 | 8.91 | 71.12 |
| 300 | 8.75 | 74.62 |

### portage-depth

| Setting | tel:portage-lakes.portage.wood-tiles |
|---|---:|
| 2 | 6.00 |
| 3 | 9.00 |
| 4 (default) | 12.00 |
| 5 | 15.00 |
| 6 | 18.00 |
| 7 | 21.00 |
| 8 | 24.00 |

### extra-trails

| Setting | tel:portage-lakes.trails.placed |
|---|---:|
| 0 | 3.00 |
| 25 (default) | 3.25 |
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
| 100 (default) | 640.00 |
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
| 100 (default) | 17573.25 | 16.91 |
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
| 100 (default) | 813.25 |
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
| 100 (default) | 64.00 |
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
| 100 (default) | 26.00 |
| 125 | 36.00 |
| 150 | 42.00 |
| 175 | 46.00 |
| 200 | 52.00 |
| 225 | 60.62 |
| 250 | 66.50 |
| 275 | 70.25 |
| 300 | 76.12 |

## Findings and fixes

1. **No broken resource scaler is demonstrated.** Wheat, renewable timber, stone, algae and fruit never decrease in the paired output curves across the default and extreme configurations. Timber telemetry increases strictly on all114 paired transitions. Total forest does decrease once:256²/four colonies/seed4, wood275→300,16,764→15,945 tiles. The scored settlement changes from proposal2 to3; the renewable planting still increases. Structural forest dominates the total, making it an unsuitable sole measure of the Wood amount control.

2. **Algae has a real upper plateau.** At250,275 and300 the actual algae count is identical within every one of the eight default seeds (mean104.75). This is isolated-pool capacity saturation, already disclosed in the map documentation. It is harmless to correctness; if distinct behavior at every advertised step is a requirement, this range needs a lower maximum or a larger eligible pool footprint. Do not imply these three values yield additional algae on default maps.

3. **Stone has deterministic quantization.**50=75,150=175 and250=275 for all eight default seeds. Small whole-tile patches explain these25-point dead steps. The full0→300 range does increase stone, but only802.12→833 mean total tiles because most rock is structural. Existing documentation explains both effects. No geometry change is needed for correctness.

4. **Extra trails is effective but capacity-limited.** Default four-colony mean count grows3→6 and never decreases in the50 paired default/extreme transitions;17 transitions are flat. The one/two-colony study below exposes more saturation on64². Five of eight one-colony compact seeds and three of eight two-colony compact seeds show no count change anywhere in0–100. Full256² has one flat one-colony seed and none with two colonies. This is an optional-route feasibility ceiling, not a globally dead control. Keep its description explicitly optional; guaranteeing a new route for every step would contradict the small geometry.

| Map / colonies | Mean trail counts at0,25,50,75,100 | Entirely flat seed curves |
|---|---|---|
| 64×64 / 1 | 0.00, 0.25, 0.38, 0.38, 0.38 | 5/8 |
| 64×64 / 2 | 1.00, 1.50, 1.75, 1.88, 1.88 | 3/8 |
| 256×256 / 1 | 0.00, 0.38, 0.62, 0.88, 0.88 | 1/8 |
| 256×256 / 2 | 1.00, 1.75, 2.88, 3.38, 3.75 | 0/8 |

5. **Elongation also reduces water area.** Every paired configuration has less pure water at the higher tested setting; the default mean falls11.37%→8.75%. Swimming savings vary with placement rather than rising monotonically. This is a meaningful landscape control, but describe it as changing lake shape, not guaranteeing increasing route savings on an individual seed.

6. **Crowded narrow low-resource cases remain close to the search bound.** The combined all-low seed97 request selects landscape21 on64×512/eight colonies and22 on512×64/eight colonies (zero-based, with24 attempts available).64²/two colonies takes landscape7. These successes do not establish a comfortable reliability margin. The separate random wheat0 failures reported during review support fixing initial planting beside the court and rerunning these exact corners after the long-compact change. This is the only concrete robustness issue here that merits a generator fix before calling the evidence final.

7. **Generation checks do not establish discovery or sustained food supply.** These JSONL files average repeated telemetry records, including service sites, fertility and mechanism savings. They cannot prove the worst individual colony or portage from those averages alone. Existing generator validations enforce their contracts; opening fog and later starvation require the separate rotation games. In particular, these passing tables must not supersede the observed narrow17 Cortex discovery failure.

## Runtime and limits

Observed wall time under the study’s concurrent workload: controls median4.627s,95th percentile7.569s, maximum20.871s; envelope median3.435s,95th percentile21.347s, maximum73.291s. These are workload timings, not isolated performance benchmarks. The slowest envelope request is512²/12 colonies/seed97.

No new build, generation run or playtest was performed for this report. The input rows do not embed a source hash or binary checksum, so keep them associated with the frozen study binary and do not silently relabel them as candidate18 evidence. The original JSONL files retain configuration and seed; control-analysis.json contains all mean curves and per-seed paired plateaus/decreases for reproduction.

Sources: controls-final/ablation.jsonl; envelope-final.jsonl; current PORTAGE_LAKES.md control contract.
