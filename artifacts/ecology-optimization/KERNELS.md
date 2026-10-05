# Ecology kernel optimization evidence

Local, ignored validation evidence for the change based on 42848408e327e7dceb877663ca04802f05504d6d. The parent task records integrated cache behavior, production tests, simulation traces and publishing evidence separately.

## Result and implementation

Land and aquatic kernels retain the exact 961 weighted offsets, signed 64-bit accumulation, division and clamping. RNG, rates, public APIs and field representation do not change. The optimization reorganizes memory access:

- Land source rows are padded across the torus, replacing inner-loop wrapping/gathers with fixed-stride reads.
- Aquatic source diagonals are laid out contiguously in temporary sheared rows. Logical-coordinate padding preserves the diagonal phase across rectangular and thin tori.
- Destination stamps split only at wrap boundaries, producing contiguous or stride-two spans with distinct destinations inside each span.
- Wrapped metadata shrinks from 91 index planes per coordinate to 91 normalized offsets. Convolution no longer allocates an unnecessary full totals buffer before replacing it; zero donor fields return immediately.

Only anonymous helpers, Field::rebuildWeighted and shoreGrowthField were edited by the kernel agent. Parent-owned GrowthCache changes share FertilityField.cpp but are a separate work area.

## Measurements

The canonical runs are `paired-release.csv` and `paired-real-release.csv`, summarized in their `.summary.json` files. Both baseline and candidate kernels are in one executable, with invocation order rotated each sample. Each case has one untimed-for-summary warmup and nine measured samples. The timer is CLOCK_PROCESS_CPUTIME_ID; source creation, hashing and output are outside the timer. Every baseline/current output hash must match after every sample. Modes 0/1 are baseline/current land; 2/3 baseline/current aquatic.

GCC 15.2.0, `-std=gnu++20 -O3 -fPIC`, matching production's relevant optimization/PIC flags, AMD Ryzen Threadripper 2950X, affinity CPU 15. The parent build/tests used CPUs 0–11 concurrently; shared-cache/bandwidth/frequency contention remains a limitation. These are repeated kernel rebuild medians, not a promise about fresh-process cold caches, whole-game speed or another platform's performance. Earlier `baseline.csv`, `diagonal.csv`, `spans.csv` and non-PIC paired results are exploratory evidence; use the release paired files for the final comparison.

### Real generated maps

The parent generated these inputs with the preserved baseline executable and recorded map commands under `maps/`. Text inputs contain dimensions followed by exact donor and inhibition/shore-support arrays; SHA256 values are in `kernel-provenance.json`.

| Map / dimensions | Land baseline → current (ms) | Aquatic baseline → current (ms) | Combined change |
|---|---:|---:|---:|
| allotments-1427 / 128² | 1.11 → 0.72 | 1.28 → 0.65 | -42.9% |
| canals-1427 / 256² | 2.17 → 1.83 | 1.31 → 0.70 | -27.4% |
| river-1427 / 512² | 41.25 → 22.75 | 37.60 → 17.56 | -48.9% |

### Synthetic stress fixtures

Seed 78311. Sparse layout:2% donors/3% inhibitors. Stripes: three donor columns and two inhibitor columns per32 columns. Dense:25% donors/25% inhibitors. Donor/inhibitor values are 256; the correctness oracles separately exercise signed and non-binary values.

| Dimensions / layout | Land baseline → current (ms) | Aquatic baseline → current (ms) | Combined change |
|---|---:|---:|---:|
| 128² / sparse | 0.80 → 0.53 | 0.82 → 0.47 | -38.5% |
| 128² / striped | 1.96 → 1.33 | 2.12 → 1.30 | -35.3% |
| 128² / dense | 9.22 → 5.83 | 9.88 → 4.71 | -44.8% |
| 256² / sparse | 3.01 → 2.05 | 2.77 → 1.68 | -35.4% |
| 256² / striped | 8.25 → 5.71 | 8.53 → 5.09 | -35.7% |
| 256² / dense | 39.17 → 22.97 | 35.58 → 17.71 | -45.6% |
| 512² / sparse | 15.30 → 8.12 | 14.72 → 7.13 | -49.2% |
| 512² / striped | 48.57 → 26.75 | 44.49 → 21.44 | -48.2% |
| 512² / dense | 181.84 → 92.00 | 172.05 → 73.24 | -53.3% |

Every measured case improves; combined gains are 27–49% on real maps and 35–53% on synthetic cases. Dense 512² still takes about165 ms for the two kernels: this reduces the rebuild cost substantially without making its density-dependent work disappear. Cache invalidation/lifecycle improvements are measured separately by the parent.

## Memory

`memory.cpp` instruments requested C++ allocation sizes with a small allocation header. `memory.csv` records peak newly-live bytes during one initially empty kernel call, excluding caller input arrays and including the returned/output field. This is allocation accounting, not process RSS or allocator overhead. Both versions retain exactly 4 bytes/tile for each resulting land/aquatic field; no new persistent kernel buffers are retained.

At 512²:

| Kernel/path | Baseline peak allocation | Current peak allocation |
|---|---:|---:|
| Land sparse/dense donor path |3,518,464bytes (3.36MiB)|3,145,728bytes (3.00MiB)|
| Land striped/convolution path |6,664,192bytes (6.36MiB)|4,194,304bytes (4.00MiB)|
| Aquatic with active donors/shore |3,518,464bytes (3.36MiB)|3,762,176bytes (3.59MiB)|
| Aquatic without shore |1,048,576bytes|1,048,576bytes|

Aquatic trades about 238 KiB additional temporary heap at512² for contiguous reads. Land peak heap decreases, especially on the convolution path. The normalized offset arrays occupy 728 bytes of bounded stack storage; old per-stamp target/donor arrays are removed. The cache's persistent rate arrays and authoritative map terrain storage are outside this kernel change.

## Correctness and review

The kernel agent's direct, cellwise oracle covers 90 cases over 1×1,1×7,7×1,2×3,3×2,23×17,17×23,31×32 and32×31, all three land paths and both aquatic sparse-source choices, zero input, signed property-range inputs and full int16/uint16 representations. Native ASan+UBSan passed (`oracle-final.log`). Independent core review extended this to 272 cases and passed both native sanitizers and ARM64/NEON under qemu with matching output hash 8659407276713522163; see the parent core-review evidence.

Independent review caught an intermediate pointer-arithmetic issue in padded accessors. Both accessors now add the nonnegative grouped padded offset before forming the pointer. Repaired source was rerun independently; no outstanding findings. The repaired -O3 object was byte-identical to the prior benchmarked object, and the final release-flags benchmarks were rebuilt from repaired source.

## Reproduction and provenance

Run `bash artifacts/ecology-optimization/reproduce-kernels.sh` from repository root. It contains the exact compiler, benchmark, summarization, sanitizer and allocation-accounting commands. `extract.py` extracts the production helper/kernel bodies without Map/GrowthCache glue, allowing the saved baseline and current functions to coexist under separate namespaces. It does not substitute an alternative algorithm. Preserved `FertilityField.baseline.cpp/.h`, `baseline-kernels.cpp`, `current-kernels.cpp` and SHA256 metadata support review. The standalone build does not run SCons or alter production binaries.
