# Gauntlet control ablation, final v9

The completed study contains **633 requests: 582 successes, 51 expected capacity refusals, and zero unexpected failures**. All refusals were 128×128 with three colonies, below the supported minimum map side. Each returned the intended larger-map/fewer-colonies message. These counts come from the complete final `ablation.jsonl`, not an intermediate study.

[Compact per-request measurements](controls-cases.csv) retain success/refusal status, seeds, dimensions, parameter values, and relevant statistics. [Summary JSON](controls-summary.json) records every sampled value, its eight-seed range, and matched-seed monotonicity checks.

## Coverage and control effects

At 256×256/four colonies, seeds 1–8 exercised **every registered value of each control individually**, including defaults: 480 successful requests (eight baselines plus 472 variations). Three additional configurations—128×128/three colonies, 512×512/six colonies, and 512×256/five colonies—used seeds 1–3 with a baseline and each control's endpoints: 153 requests. This covers each slider's full discrete range in isolation; it is not the Cartesian product of all controls or an exhaustive size/team/seed proof.

| Control | Observed effect at 256×256/four colonies |
| --- | --- |
| Court size, 80–120 by 10 | Court radial width increases 30 → 31.104 → 34.56 → 38.016 → 41.472 tiles. The 80% setting reaches the 30-tile lower clamp; subsequent steps remain distinct. Larger courts trade home-farm space for arena space, so total wheat is not the appropriate primary metric. |
| Gate width, 5/7/9 | Measured gate-width telemetry equals each requested value. This is a geometry metric, not a claim that tower fire covers every tile of a wide gate. |
| Partition thickness, 1/2/3 | Telemetry matches each requested width. Total structural stone strictly increases for every matched seed: ranges 4,396–4,504, 4,520–4,636, and 4,624–4,732 tiles. |
| Starting tower level, 0–3 | Zero disables towers; enabled levels retain two towers per home and measured tower level equals 1, 2, or 3. Constant building counts across levels are expected, **not a dead control step**. |
| Wheat amount, 0–300 by 25 | Total wheat increases without reversals from 360 opening tiles at 0% to 5,707–6,232 tiles at the high end. Finite beds saturate: all eight seeds plateau from 275% to 300%, and seed 2 also plateaus from 250% to 275%. |
| Wood amount, 0–300 by 25 | Exactly 120 → 360 total tiles, increasing by 20 each step. Zero retains the intended 30-tile opening supply per home. |
| Algae amount, 0–300 by 25 | Zero removes algae; every step increases algae for each matched seed, reaching 917–983 tiles at 300%. Seed-dependent shallow-water choices explain the range. |
| Fruit amount, 0–300 by 25 | Zero removes fruit. Every step increases each of the three fruit types; each type reaches 60 tiles at 300%. Integer planting quotas produce unequal-sized increments, not reversals. |

The primary statistic for every control is nondecreasing in all matched-seed comparisons. Wheat is the only control with an unchanged adjacent step in this sample. The retained CSV also includes farm area, court wheat, all fruit types, and construction-site counts so secondary effects can be inspected.

## Remaining limitation

Wheat is a planting request, not a promise of proportional realized abundance. Its upper settings are redundant once a particular layout's beds fill. Reducing the global maximum would remove useful range on larger layouts; redefining the control as a fraction of bed capacity would change its meaning. The current finite-bed plateau is therefore retained and documented. These measurements establish generation reliability and control response in the stated sample; they do not establish human enjoyment or every possible parameter combination.

## Reproduction

The original final run used the frozen `/tmp/gauntlet-evidence/glob2-v9` binary. Repository reproduction after building the current executable:

```sh
python3 .agents/skills/glob2-map-design/scripts/control_study.py gauntlet ablation --binary build/src/glob2 --seeds 8 --jobs 4 --teams 2-12 --out artifacts/gauntlet/controls-v9
```

The study's `--teams` option does not make this ablation exhaustive over team counts; the actual configurations are listed above. The complete local run data is retained at `artifacts/gauntlet/controls-v9/ablation.jsonl`; the frozen binary is retained at `artifacts/gauntlet/glob2-v9`. These larger local artifacts are separate from the compact repository evidence linked above. Timing was collected under shared-machine load and is not used to judge slider effectiveness.
