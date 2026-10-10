# Final PR 1045: automatic-mode audit and master comparison

PR `13843165bd7e778650142fe884c7af37b40b56f1` versus master `07442b5919988edb323efdf571f220e657a30492`.

## Finding

Automatic mode follows the conservative established-plan milestone, but there is no production adaptive selector. All decision slots start as CPU. Only test code calls `establish()`. Passive accounting does not publish decisions, and is disabled by default. The current automatic mode cannot discover GPU winners or achieve the better of CPU and GPU automatically. No alternative CPU algorithm is retained.

This is an implementation-scope limit, not a conclusion inferred from benchmark timing. The source audit and runtime assertions are included in the evidence.

## Coverage

Measured **412 runs across 103 immutable maps**, including **98 512² maps**. Each map has a rotated matched quartet: master CPU, PR CPU, PR forced OpenCL (`Frozen8` where eligible/ready), PR automatic. Eight compute slots include the owner (seven workers). Runs have a 5,000-tick cap and a 1,000-tick warmup; one timing per configuration/map. Initial/final simulation checksums, final save hashes and seven gradient work counts agree across each quartet.

Completed 412 of 632 planned runs; 55 roster maps remained unmeasured at the time cutoff. Raw completion and hash receipts are in `complete.json`.

The planned roster covers all 67 playable generators with two independent 512² seeds each, 16 fresh 128²/256² controls and eight retained fixtures. The editor-only uniform generator is excluded. The generator catalog supports at most 512²; no 1024² generator games were claimed. Kernel qualification holdouts were not used. The protocol and roster were frozen before timed runs. The two-hour admission budget includes build and fixture preparation; a budget stop occurs only between complete quartets.

## CPU consumption

Primary metric: process CPU consumed by all game threads per measured warm simulation tick, including required-work drain. It measures actual CPU consumption, but includes game/AI work as well as gradients. It is not an isolated kernel CPU measurement. Negative percentages mean less CPU. Equal-map geometric mean ratios avoid letting one long-running map dominate. The first-seed catalog column gives each generator equal weight; the full measured set can give extra weight to generators whose second seed fit within the time budget.

| Comparison | All maps | 512² maps | One 512² seed per generator |
|---|---:|---:|---:|
| Automatic vs master CPU | +1.13% | +1.24% | +1.11% |
| PR CPU vs master CPU | +1.09% | +1.14% | +1.20% |
| PR OpenCL vs master CPU | -10.53% | -11.30% | -11.63% |
| Automatic vs PR CPU | +0.03% | +0.10% | -0.09% |
| PR OpenCL vs PR CPU | -11.50% | -12.30% | -12.68% |

On all maps, forced OpenCL used less warm process CPU than forced CPU in 95/103 observations. Automatic versus the lower observed forced-plan CPU cost: median +13.65%, worst +30.32%. This observed minimum is a noisy hindsight reference, not an achievable per-request oracle or a significance test.

On 512² maps, forced OpenCL used less warm process CPU than forced CPU in 93/98 observations. Automatic versus the lower observed forced-plan CPU cost: median +14.16%, worst +30.32%. This observed minimum is a noisy hindsight reference, not an achievable per-request oracle or a significance test.

Whole-process CPU includes startup, save, teardown and initialization. These deltas use the separate `wait4` user+system CPU measurement.

| Configuration vs master | All maps | 512² maps | One 512² seed per generator |
|---|---:|---:|---:|
| pr-cpu | +0.89% | +0.92% | +1.06% |
| pr-auto | +1.11% | +1.15% | +1.10% |
| pr-opencl | -8.38% | -9.23% | -9.46% |

## Timing and memory guards

These are separate from the CPU-efficiency conclusion. Summed gradient callback elapsed time overlaps across workers and includes waits. Preparation is nested inside callback time; do not add it again. Whole-game runtime does not measure total CPU work.

| Metric vs master | Auto: all maps | Auto: 512² | OpenCL: all maps | OpenCL: 512² |
|---|---:|---:|---:|---:|
| Simulation elapsed runtime | -0.82% | -0.81% | +6.83% | +6.51% |
| Tick p95 | -4.11% | -4.25% | +23.28% | +23.89% |
| Tick p99 | -2.31% | -2.44% | +9.91% | +10.06% |
| Peak process RSS | -0.00% | +0.01% | +23.66% | +21.01% |

The per-map table and raw outputs retain every tested case, including regressions. Common gradient join-wait totals include fixed-deadline, finish and cleanup joins. The PR-only publication-wait subset is preserved separately in raw results and is not treated as a comparable master counter.


## Publication waits: complete first-seed catalog

PR-only fixed-deadline publication-wait counters, full 5,000-tick segments. These are main-thread wait totals, not summed worker execution time. Master does not expose this narrower counter.

| PR configuration | Mean publication wait per game |
|---|---:|
| pr-cpu | 596.70 ms |
| pr-auto | 598.43 ms |
| pr-opencl | 314.65 ms |

Forced OpenCL vs PR CPU, total publication waiting across the catalog: -47.27%. This can improve while headless elapsed time or tick tails worsen; the counters measure different effects and do not identify the cause of the remaining runtime difference.

## Automatic-mode contract

Every timed automatic and CPU run asserts no GPU initialization/readiness and no accounting recording/processing. The contract-only preflight also reports no probes or retained inputs. Optional telemetry uses elapsed timings, not per-request CPU consumption; CPU-oriented learned selection would need an appropriate signal or independently validated profiles. Forced OpenCL starts each game in a fresh process, initializes/compiles on an existing worker and falls back to CPU until ready. Existing driver compilation caches are not cleared, so this is not a cold-driver-cache qualification. Owner-thread, unsupported and resumable operations retain CPU execution; forcing the backend is not a promise that all gradient work uses the GPU. Readiness at warm-window start/end is included per case; readiness alone does not establish that every gradient used the GPU.

## Reproduction and limitations

- AMD Ryzen Threadripper 2950X host; release GCC 15.2 Linux x86-64 builds, `-O3`, identical compiler flags and linked dependency hashes; NVIDIA RTX 2070 SUPER / driver 580.178.04. No development/PCH/unity builds. Exact build commands and logs included.
- Immutable input saves, generator seeds/parameters, binary and dependency hashes, protocol, command logs, raw results, analysis scripts and per-map tables are archived. Original commands contain local paths that need remapping.
- The repository cgroup-v2 wrapper reserved cores 0–7 and SMT siblings 16–23 in an exclusive balanced partition. Games used 0–7 and the affinity observer used CPU 23 every 0.5 seconds. Kernel partition validity was checked each second. Earlier affinity-only trials encountered an unrelated test resetting affinity; those trials are retained separately and not pooled. No observed overlap is accepted. This does not isolate shared caches, memory bandwidth or desktop GPU contention. GPU state snapshots are included, not continuous occupancy or energy measurements.
- One timing per configuration/map trades precision for generator breadth. Small percentage differences, per-map wins and worst cases are descriptive, not proof of repeatable benefit. Independent seeds do not replace repeated timing.
- These are 5,000-tick headless segments, not full early/middle/late-game qualification or rendering-contention tests. No final qualification/admission claim is made for any algorithm.
- This campaign verifies Linux simulation endpoints, identical saves and work counts. It does not add cross-platform per-tick, replay or continuation verification. Earlier PR verification remains separately linked in the PR.
- The two-hour budget and any unmeasured roster cases are explicit; no timing outlier is dropped. Earlier PR builds are not relabeled as master.

## Every measured map

| Map | Size | Auto / master CPU | Auto / PR CPU | OpenCL / PR CPU |
|---|---:|---:|---:|---:|
| open-small | 128² | -0.20% | +0.31% | -0.59% |
| ocean-islands | 256² | +1.71% | -0.14% | +0.58% |
| maze-routes | 256² | +1.99% | +0.58% | +34.58% |
| urban-canals | 256² | -9.43% | -10.59% | -7.69% |
| river-network | 512² | +2.49% | +2.15% | -16.34% |
| mountain-passes | 512² | -0.06% | +1.94% | -16.95% |
| wet-small | 128² | +1.22% | +4.24% | +6.38% |
| central-chokes | 512² | +2.43% | -0.48% | -3.41% |
| fingerprint-512-seed1 | 512² | +0.78% | -0.67% | -13.42% |
| old-town-512-seed1 | 512² | +3.73% | -1.08% | -14.05% |
| symmetric-arena-512-seed1 | 512² | +1.55% | +3.98% | -17.57% |
| swamp-512-seed1 | 512² | +1.63% | +4.15% | -11.83% |
| tidal-flats-512-seed1 | 512² | -1.50% | +0.97% | -15.48% |
| river-512-seed1 | 512² | +1.98% | -0.30% | -10.67% |
| isles-512-seed1 | 512² | +1.31% | -0.40% | -5.25% |
| ring-world-512-seed1 | 512² | +1.09% | -0.26% | -7.55% |
| amphitheatre-512-seed1 | 512² | -2.39% | -2.84% | -8.65% |
| shattered-coast-512-seed1 | 512² | +1.85% | +0.50% | -14.02% |
| crater-lakes-512-seed1 | 512² | +3.98% | +2.29% | -17.86% |
| fjord-continent-512-seed1 | 512² | +3.04% | +0.82% | -4.06% |
| spider-web-512-seed1 | 512² | +2.25% | +0.57% | -7.43% |
| concrete-islands-512-seed1 | 512² | -0.41% | -2.85% | -7.91% |
| watershed-512-seed1 | 512² | +1.70% | +1.06% | -16.32% |
| maze-512-seed1 | 512² | -0.00% | -2.06% | -14.12% |
| islands-512-seed1 | 512² | +2.29% | +1.22% | +1.12% |
| stone-highlands-512-seed1 | 512² | +0.75% | -0.30% | -19.49% |
| switchbacks-512-seed1 | 512² | -2.83% | +0.29% | -7.00% |
| city-states-512-seed1 | 512² | +1.70% | +0.57% | -6.18% |
| canals-512-seed1 | 512² | -1.36% | -2.04% | -10.73% |
| sierpinski-gardens-512-seed1 | 512² | +0.22% | -1.80% | -17.35% |
| hilbert-river-512-seed1 | 512² | +3.11% | +0.86% | -19.92% |
| lava-shield-512-seed1 | 512² | -0.35% | -3.10% | -16.96% |
| honeycomb-isle-512-seed1 | 512² | +3.24% | +1.16% | +1.44% |
| karst-towers-512-seed1 | 512² | +1.74% | -0.62% | -16.65% |
| bajada-512-seed1 | 512² | +1.55% | +1.01% | -18.11% |
| central-quarry-512-seed1 | 512² | +1.04% | +0.70% | -17.70% |
| hidden-oasis-512-seed1 | 512² | +2.63% | +0.97% | -22.52% |
| drowned-forest-512-seed1 | 512² | +0.62% | -0.05% | -9.44% |
| portage-lakes-512-seed1 | 512² | +1.81% | +1.63% | -13.49% |
| orchard-commons-512-seed1 | 512² | +3.89% | +2.66% | -4.99% |
| last-treeline-512-seed1 | 512² | -1.23% | -1.87% | -14.92% |
| gauntlet-512-seed1 | 512² | -1.92% | +1.19% | -7.61% |
| faulted-city-512-seed1 | 512² | +3.50% | +0.07% | -20.67% |
| comb-512-seed1 | 512² | +4.14% | +1.11% | -6.33% |
| encircled-kingdom-512-seed1 | 512² | -1.56% | -2.25% | -13.47% |
| bastion-keys-512-seed1 | 512² | -2.06% | -5.17% | -13.83% |
| even-ground-512-seed1 | 512² | +1.04% | -0.16% | -11.90% |
| marchland-512-seed1 | 512² | +0.25% | -1.19% | -19.26% |
| who-ate-the-map-512-seed1 | 512² | +1.85% | +0.77% | -6.45% |
| rugged-archipelago-512-seed1 | 512² | +4.05% | +0.63% | -7.17% |
| hungry-marches-512-seed1 | 512² | +2.35% | +0.22% | -22.42% |
| contested-commons-512-seed1 | 512² | +3.51% | +2.03% | +0.16% |
| rain-shadow-512-seed1 | 512² | -0.24% | -1.01% | -19.28% |
| everglades-512-seed1 | 512² | +2.06% | -0.01% | -9.14% |
| polder-512-seed1 | 512² | -1.61% | -3.22% | -15.97% |
| carousel-512-seed1 | 512² | -0.13% | -1.22% | -4.48% |
| old-growth-512-seed1 | 512² | +1.53% | -0.09% | -6.78% |
| anthill-512-seed1 | 512² | +1.55% | +0.64% | -3.82% |
| coral-512-seed1 | 512² | +2.05% | +2.67% | -0.90% |
| emoji-512-seed1 | 512² | -1.57% | -0.36% | -12.53% |
| forts-512-seed1 | 512² | +0.88% | +1.78% | -13.58% |
| braided-delta-512-seed1 | 512² | -0.70% | +2.19% | -17.38% |
| breachable-highlands-512-seed1 | 512² | +1.81% | -0.20% | -15.77% |
| hedgerow-country-512-seed1 | 512² | +0.96% | +0.07% | -22.33% |
| glacis-512-seed1 | 512² | +1.72% | +0.25% | -15.78% |
| allotments-512-seed1 | 512² | +1.36% | -0.42% | -20.61% |
| caravanserai-512-seed1 | 512² | -0.23% | -1.26% | -20.63% |
| braided-river-512-seed1 | 512² | +2.94% | -0.43% | -17.73% |
| drumlin-field-512-seed1 | 512² | +0.47% | -0.69% | -9.00% |
| continents-512-seed1 | 512² | +0.42% | -1.74% | -2.60% |
| savannah-512-seed1 | 512² | +5.29% | -0.40% | -22.95% |
| hills-512-seed1 | 512² | +2.17% | -1.77% | -24.01% |
| rice-terraces-512-seed1 | 512² | -1.29% | -2.33% | -14.06% |
| locust-512-seed1 | 512² | -0.72% | -0.55% | -5.64% |
| plantations-512-seed1 | 512² | +2.51% | +0.81% | -10.95% |
| fingerprint-512-seed2 | 512² | +0.18% | -1.08% | -13.85% |
| old-town-512-seed2 | 512² | +0.83% | +1.69% | -11.02% |
| symmetric-arena-512-seed2 | 512² | +1.18% | +1.52% | -19.32% |
| swamp-512-seed2 | 512² | +3.82% | -0.80% | -14.21% |
| tidal-flats-512-seed2 | 512² | +0.90% | -1.41% | -18.61% |
| river-512-seed2 | 512² | -1.33% | -2.90% | -13.57% |
| isles-512-seed2 | 512² | +0.00% | -3.68% | -5.74% |
| ring-world-512-seed2 | 512² | +1.16% | +0.67% | -6.55% |
| amphitheatre-512-seed2 | 512² | +3.33% | +2.90% | -8.47% |
| shattered-coast-512-seed2 | 512² | +0.05% | +1.73% | -12.85% |
| crater-lakes-512-seed2 | 512² | +1.08% | +0.13% | -16.71% |
| fjord-continent-512-seed2 | 512² | +3.22% | +1.98% | -5.23% |
| spider-web-512-seed2 | 512² | +2.34% | -0.04% | -8.90% |
| concrete-islands-512-seed2 | 512² | +0.86% | +0.32% | -8.43% |
| watershed-512-seed2 | 512² | +2.28% | +2.40% | -16.63% |
| maze-512-seed2 | 512² | +0.04% | -0.26% | +18.20% |
| islands-512-seed2 | 512² | +3.46% | +0.34% | -5.79% |
| stone-highlands-512-seed2 | 512² | +1.79% | +0.60% | -21.05% |
| switchbacks-512-seed2 | 512² | +0.14% | -0.65% | -10.59% |
| city-states-512-seed2 | 512² | +0.32% | -1.36% | -8.45% |
| canals-512-seed2 | 512² | +1.39% | -0.42% | -11.95% |
| sierpinski-gardens-512-seed2 | 512² | +5.36% | +3.15% | -16.24% |
| hilbert-river-512-seed2 | 512² | +0.84% | +0.33% | -16.37% |
| lava-shield-512-seed2 | 512² | +3.87% | +2.12% | -13.57% |
| honeycomb-isle-512-seed2 | 512² | +4.16% | +1.64% | +1.98% |
| karst-towers-512-seed2 | 512² | -0.40% | +0.80% | -10.43% |
| bajada-512-seed2 | 512² | +0.44% | +1.87% | -19.54% |
| central-quarry-512-seed2 | 512² | +0.85% | +0.62% | -16.02% |

## Unmeasured roster cases

Fresh 512² generator coverage: 67/67; generators with both large seeds: 28/67.

hidden-oasis-512-seed2, drowned-forest-512-seed2, portage-lakes-512-seed2, orchard-commons-512-seed2, last-treeline-512-seed2, gauntlet-512-seed2, faulted-city-512-seed2, comb-512-seed2, encircled-kingdom-512-seed2, bastion-keys-512-seed2, even-ground-512-seed2, marchland-512-seed2, who-ate-the-map-512-seed2, rugged-archipelago-512-seed2, hungry-marches-512-seed2, contested-commons-512-seed2, rain-shadow-512-seed2, everglades-512-seed2, polder-512-seed2, carousel-512-seed2, old-growth-512-seed2, anthill-512-seed2, coral-512-seed2, emoji-512-seed2, forts-512-seed2, braided-delta-512-seed2, breachable-highlands-512-seed2, hedgerow-country-512-seed2, glacis-512-seed2, allotments-512-seed2, caravanserai-512-seed2, braided-river-512-seed2, drumlin-field-512-seed2, continents-512-seed2, savannah-512-seed2, hills-512-seed2, rice-terraces-512-seed2, locust-512-seed2, plantations-512-seed2, even-ground-128-control, even-ground-256-control, islands-128-control, islands-256-control, maze-128-control, maze-256-control, canals-128-control, canals-256-control, watershed-128-control, watershed-256-control, bajada-128-control, bajada-256-control, swamp-128-control, swamp-256-control, contested-commons-128-control, contested-commons-256-control
