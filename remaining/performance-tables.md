# Resource-growth optimization experiments

Baseline: `edb09d40204a1fdb3a6d0e5934ef34e9c344de60`. Candidate confirmation master: `0f1a2569ab7f23c8702a078978054f73f4ddb9cc`. Final retained comparison master: `6487b873dd3f29ad3ab75c7513597fa47905c132`. Linux x86-64, GCC 15.2, release/O3. See frozen inputs, build commands, fixture hashes, and reservation audits in this directory.

Positive throughput and CPU-reduction values favor the candidate. Intervals are paired bootstrap 95% intervals (2,000 resamples); absolute savings are medians of paired differences, not differences between independent medians. Warm-ups are excluded. No measured slow runs are discarded.

# Extended comparisons

Raw measurements and metadata: [`extended/`](./extended/).

## A

| Scenario | Pairs | Throughput change [95% CI] | Engine CPU reduction [95% CI] | Wall saved / 1024 ticks | CPU saved / 1024 ticks | p99 change | Peak RSS change |
|---|---:|---|---|---:|---:|---:|---:|
| ai512 | 10 | +1.16% [-0.83, +2.64] | +1.16% [+0.12, +2.69] | +16.01 ms | +52.64 ms | -1.96% [-9.37, +2.40] | +0.16% [-1.75, +1.26] |
| dense | 30 | -0.22% [-2.09, +0.47] | -0.33% [-2.22, +0.70] | -1.06 ms | -6.27 ms | +5.21% [-0.46, +9.05] | +0.00% [+0.00, +0.02] |
| disabled512 | 10 | -0.98% [-3.03, +2.42] | -0.07% [-3.54, +2.42] | -10.50 ms | -2.31 ms | +6.82% [-1.17, +22.90] | -0.64% [-1.17, -0.00] |
| fragmented | 10 | +0.04% [-2.51, +2.02] | -0.03% [-4.41, +2.22] | +0.87 ms | -3.19 ms | +0.55% [-6.03, +7.47] | -0.78% [-2.35, +1.86] |
| harvested | 10 | -0.91% [-2.87, +1.41] | -0.67% [-1.96, +0.76] | -4.53 ms | -9.29 ms | +0.19% [-1.26, +3.92] | +0.00% [+0.00, +0.00] |
| multi | 10 | +3.80% [-1.95, +6.96] | +1.73% [-0.42, +2.55] | +155.57 ms | +220.29 ms | +0.84% [-14.14, +11.13] | +1.31% [-2.22, +3.93] |
| saturated | 10 | -1.71% [-3.91, +1.21] | -1.95% [-3.96, +1.30] | -8.43 ms | -37.97 ms | +4.62% [+0.10, +9.66] | +0.00% [-0.14, +0.15] |
| sparse | 10 | +1.50% [-8.11, +8.84] | +0.93% [-6.25, +6.81] | +1.47 ms | +3.31 ms | -5.59% [-8.68, +0.48] | +0.00% [+0.00, +0.00] |

## AB

| Scenario | Pairs | Throughput change [95% CI] | Engine CPU reduction [95% CI] | Wall saved / 1024 ticks | CPU saved / 1024 ticks | p99 change | Peak RSS change |
|---|---:|---|---|---:|---:|---:|---:|
| ai512 | 10 | +0.80% [-1.45, +2.17] | +2.54% [-1.22, +3.04] | +11.16 ms | +116.65 ms | +0.71% [-2.94, +2.80] | -0.89% [-1.05, +1.27] |
| dense | 30 | +0.01% [-2.08, +2.11] | +0.05% [-2.25, +2.06] | +0.08 ms | +1.11 ms | +1.38% [-3.14, +4.34] | +0.00% [+0.00, +0.00] |
| disabled512 | 10 | +0.56% [-1.46, +3.32] | +0.98% [-1.62, +3.08] | +5.93 ms | +33.32 ms | -0.62% [-9.91, +9.58] | +0.12% [-0.27, +1.30] |
| fragmented | 10 | -1.73% [-3.28, +0.46] | -1.51% [-4.34, +0.87] | -46.06 ms | -150.56 ms | +3.53% [-0.77, +7.22] | +0.22% [-1.70, +1.23] |
| harvested | 10 | +0.66% [-1.11, +2.65] | +1.54% [-0.81, +3.86] | +3.21 ms | +21.40 ms | +0.35% [-2.11, +3.20] | +0.00% [-0.12, +0.00] |
| multi | 11 | +1.89% [-5.97, +5.27] | -0.20% [-2.19, +1.82] | +73.57 ms | -28.29 ms | +3.42% [-5.75, +5.17] | -0.39% [-1.87, +1.38] |
| saturated | 10 | +1.10% [-1.66, +3.70] | +1.19% [-1.68, +3.68] | +5.40 ms | +23.41 ms | +0.11% [-1.72, +3.87] | +0.00% [-0.14, +0.00] |
| sparse | 10 | +2.58% [-4.28, +8.54] | +1.32% [-2.46, +4.42] | +2.61 ms | +4.95 ms | -3.15% [-11.15, +7.71] | +0.00% [+0.00, +0.00] |

## ABCD

| Scenario | Pairs | Throughput change [95% CI] | Engine CPU reduction [95% CI] | Wall saved / 1024 ticks | CPU saved / 1024 ticks | p99 change | Peak RSS change |
|---|---:|---|---|---:|---:|---:|---:|
| ai512 | 10 | +2.56% [+0.98, +3.29] | +3.64% [+0.38, +4.76] | +37.02 ms | +168.78 ms | +1.36% [-2.21, +4.19] | +0.08% [-0.38, +1.29] |
| dense | 30 | +2.96% [+1.87, +4.42] | +2.99% [+2.33, +4.32] | +13.62 ms | +56.50 ms | +4.38% [-0.25, +8.90] | +0.00% [+0.00, +0.00] |
| disabled512 | 30 | +0.72% [-0.50, +1.59] | +0.71% [-0.08, +1.83] | +7.39 ms | +23.51 ms | +2.83% [-0.66, +7.49] | +0.00% [+0.00, +0.05] |
| fragmented | 30 | +1.61% [+0.61, +2.22] | +1.92% [+0.47, +2.89] | +41.00 ms | +191.69 ms | +0.61% [-2.19, +2.65] | -0.34% [-1.46, +0.12] |
| harvested | 10 | +1.67% [+0.88, +2.46] | +1.20% [+0.36, +2.64] | +8.17 ms | +17.03 ms | -0.17% [-2.79, +2.74] | +0.00% [-0.13, +0.00] |
| multi | 30 | +1.58% [-1.14, +4.84] | +1.69% [+0.64, +2.88] | +62.85 ms | +219.70 ms | -4.09% [-9.62, +0.12] | +0.41% [-1.55, +1.76] |
| saturated | 10 | +2.75% [-1.12, +6.36] | +2.87% [-1.16, +6.20] | +12.64 ms | +53.35 ms | -1.91% [-6.86, +0.82] | +0.00% [-0.14, +0.14] |
| sparse | 10 | +5.33% [+1.31, +11.80] | +3.92% [+1.00, +7.34] | +5.20 ms | +14.45 ms | -7.20% [-10.91, -2.69] | +0.00% [-0.16, +0.00] |

## B

| Scenario | Pairs | Throughput change [95% CI] | Engine CPU reduction [95% CI] | Wall saved / 1024 ticks | CPU saved / 1024 ticks | p99 change | Peak RSS change |
|---|---:|---|---|---:|---:|---:|---:|
| ai512 | 25 | +2.21% [+0.54, +3.20] | +2.83% [+1.36, +4.63] | +33.11 ms | +124.64 ms | -3.45% [-5.53, +2.10] | +0.06% [-0.49, +0.24] |
| dense | 30 | +0.05% [-1.59, +2.05] | +0.46% [-1.22, +1.87] | +0.21 ms | +8.72 ms | +1.33% [-1.11, +3.61] | +0.00% [+0.00, +0.16] |
| disabled512 | 10 | +1.41% [-1.84, +2.81] | +1.47% [-1.82, +3.70] | +14.52 ms | +49.47 ms | -3.01% [-6.76, +5.07] | -0.11% [-0.37, +0.12] |
| fragmented | 10 | -1.08% [-1.87, +1.25] | -1.23% [-1.92, +0.88] | -27.13 ms | -122.93 ms | +0.99% [-1.95, +4.15] | -1.23% [-1.65, +0.48] |
| harvested | 10 | -0.07% [-1.46, +2.34] | +0.47% [-0.40, +2.68] | -0.32 ms | +6.49 ms | +0.83% [-4.95, +5.32] | +0.00% [+0.00, +0.00] |
| multi | 30 | +1.89% [-2.58, +3.99] | -0.76% [-1.72, +0.31] | +67.87 ms | -98.19 ms | +0.54% [-3.50, +9.79] | -0.04% [-0.85, +1.49] |
| saturated | 10 | -0.74% [-2.66, +1.25] | -0.94% [-3.47, +1.57] | -3.59 ms | -17.80 ms | +0.23% [-4.72, +4.97] | +0.00% [+0.00, +0.00] |
| sparse | 10 | -0.19% [-4.55, +3.41] | +0.03% [-3.03, +2.64] | -0.18 ms | +0.11 ms | +0.49% [-11.64, +10.63] | +0.00% [+0.00, +0.00] |

## C

| Scenario | Pairs | Throughput change [95% CI] | Engine CPU reduction [95% CI] | Wall saved / 1024 ticks | CPU saved / 1024 ticks | p99 change | Peak RSS change |
|---|---:|---|---|---:|---:|---:|---:|
| ai512 | 10 | +1.04% [-0.93, +1.70] | +0.66% [-1.26, +2.43] | +14.58 ms | +28.95 ms | +0.68% [-2.79, +6.26] | -1.10% [-1.22, +0.09] |
| dense | 10 | +3.15% [-0.37, +6.47] | +3.67% [-0.72, +5.98] | +14.03 ms | +66.38 ms | +1.90% [-9.10, +7.41] | +0.71% [-0.27, +1.15] |
| disabled512 | 30 | -0.42% [-1.17, +0.85] | -0.87% [-2.38, +0.30] | -4.46 ms | -28.54 ms | -1.49% [-4.25, +0.58] | +0.00% [+0.00, +0.03] |
| fragmented | 10 | +0.38% [-0.88, +2.73] | +0.50% [-1.75, +3.31] | +10.14 ms | +44.59 ms | +0.24% [-3.83, +2.59] | -0.01% [-0.95, +0.39] |
| harvested | 10 | -0.07% [-1.91, +1.10] | +0.59% [-0.92, +1.43] | -0.37 ms | +8.42 ms | +2.19% [-1.47, +4.78] | +0.00% [+0.00, +0.00] |
| multi | 10 | +6.66% [+2.38, +8.93] | +2.64% [+1.39, +4.54] | +289.29 ms | +377.09 ms | -1.85% [-17.60, +5.16] | -1.00% [-2.57, +2.29] |
| saturated | 10 | +0.96% [-0.93, +3.97] | +1.63% [-0.79, +4.03] | +4.61 ms | +31.23 ms | +4.99% [-0.56, +10.31] | +0.00% [+0.00, +0.15] |
| sparse | 10 | +3.02% [-0.47, +4.77] | +1.55% [-0.98, +2.97] | +2.88 ms | +5.63 ms | -1.12% [-5.85, +6.84] | +0.00% [-0.16, +0.00] |

## D

| Scenario | Pairs | Throughput change [95% CI] | Engine CPU reduction [95% CI] | Wall saved / 1024 ticks | CPU saved / 1024 ticks | p99 change | Peak RSS change |
|---|---:|---|---|---:|---:|---:|---:|
| ai512 | 10 | -0.22% [-0.85, +2.42] | +0.27% [-0.68, +2.72] | -3.13 ms | +11.74 ms | -1.94% [-6.14, -0.55] | +0.02% [-1.21, +0.73] |
| dense | 10 | +1.98% [-0.21, +6.67] | +2.30% [-0.70, +6.78] | +9.05 ms | +42.29 ms | -0.62% [-7.10, +3.65] | +0.58% [+0.04, +1.09] |
| disabled512 | 10 | -0.48% [-1.02, +2.73] | -1.46% [-3.70, +3.23] | -5.09 ms | -49.49 ms | -4.17% [-10.73, +0.94] | -0.01% [-0.32, +0.63] |
| fragmented | 10 | +1.47% [+0.20, +2.41] | +2.79% [-0.40, +3.68] | +35.47 ms | +263.91 ms | +0.84% [-3.30, +5.69] | +0.61% [-0.96, +1.00] |
| harvested | 10 | +1.02% [+0.39, +2.39] | +0.94% [+0.14, +1.80] | +5.02 ms | +13.29 ms | -2.32% [-5.69, +0.35] | +0.00% [-0.13, +0.00] |
| multi | 30 | +0.57% [-4.28, +4.80] | +0.76% [-0.03, +1.68] | +24.67 ms | +102.80 ms | -3.24% [-8.93, +0.36] | -0.31% [-2.74, +1.01] |
| saturated | 10 | +0.15% [-1.89, +2.19] | -0.05% [-2.52, +2.41] | +0.72 ms | -0.80 ms | +1.43% [-0.82, +8.14] | +0.00% [-0.14, +0.00] |
| sparse | 10 | +6.42% [+0.65, +15.26] | +3.36% [+0.04, +8.80] | +6.32 ms | +12.45 ms | -1.00% [-5.94, +4.42] | +0.00% [+0.00, +0.17] |

## control

| Scenario | Pairs | Throughput change [95% CI] | Engine CPU reduction [95% CI] | Wall saved / 1024 ticks | CPU saved / 1024 ticks | p99 change | Peak RSS change |
|---|---:|---|---|---:|---:|---:|---:|
| ai512 | 10 | +0.81% [-0.71, +2.55] | +1.45% [-0.92, +3.38] | +11.42 ms | +69.39 ms | -1.89% [-7.47, +3.66] | -0.79% [-1.40, -0.18] |
| dense | 10 | +0.87% [-0.99, +3.83] | +0.87% [-1.30, +4.36] | +4.32 ms | +16.96 ms | -0.80% [-4.90, +6.62] | +0.28% [-0.22, +0.44] |
| disabled512 | 10 | -0.11% [-1.58, +1.69] | +1.35% [-2.81, +2.05] | -1.18 ms | +45.78 ms | -0.45% [-5.87, +14.19] | +0.09% [-0.41, +0.20] |
| fragmented | 30 | +0.01% [-1.08, +0.70] | +0.22% [-1.49, +0.79] | +0.30 ms | +22.66 ms | -0.06% [-3.92, +2.56] | +0.36% [-0.58, +0.61] |
| harvested | 10 | -0.36% [-2.08, +0.65] | -0.59% [-2.20, +0.24] | -1.75 ms | -8.24 ms | +0.13% [-2.07, +4.25] | +0.00% [-0.12, +0.00] |
| multi | 10 | -1.13% [-6.89, +4.72] | +0.73% [-3.33, +2.73] | -39.21 ms | +94.48 ms | -0.75% [-5.81, +8.02] | -1.12% [-4.62, +2.31] |
| saturated | 10 | +0.31% [-0.93, +1.94] | +0.24% [-1.17, +2.21] | +1.45 ms | +4.48 ms | -2.55% [-7.04, -0.53] | +0.00% [+0.00, +0.14] |
| sparse | 10 | +0.13% [-4.67, +3.47] | -1.07% [-2.77, +1.89] | +0.13 ms | -4.01 ms | +0.53% [-4.00, +7.76] | +0.00% [+0.00, +0.00] |

## master

| Scenario | Pairs | Throughput change [95% CI] | Engine CPU reduction [95% CI] | Wall saved / 1024 ticks | CPU saved / 1024 ticks | p99 change | Peak RSS change |
|---|---:|---|---|---:|---:|---:|---:|
| ai512 | 10 | +0.53% [-1.06, +1.98] | +2.56% [+1.42, +4.30] | +7.49 ms | +115.34 ms | Unavailable | -0.12% [-0.64, +0.07] |
| dense | 10 | -5.96% [-6.72, -3.14] | -5.75% [-6.47, -2.79] | -30.34 ms | -106.25 ms | Unavailable | -1.71% [-2.06, -0.91] |
| disabled512 | 10 | -0.98% [-3.39, +1.03] | -0.96% [-4.84, +1.42] | -10.47 ms | -33.43 ms | Unavailable | +0.15% [-0.74, +1.28] |
| fragmented | 10 | -10.58% [-13.13, -9.05] | -4.11% [-9.00, -3.47] | -324.70 ms | -425.62 ms | Unavailable | -8.26% [-10.44, -3.43] |
| harvested | 10 | -3.33% [-5.68, -0.38] | -3.19% [-4.64, -1.09] | -16.90 ms | -44.63 ms | Unavailable | +0.00% [-0.12, +0.00] |
| multi | 10 | -30.38% [-32.58, -28.47] | -4.10% [-5.02, -1.77] | -1833.79 ms | -548.77 ms | Unavailable | -23.38% [-24.92, -21.18] |
| saturated | 10 | -6.88% [-9.92, -5.58] | -6.83% [-10.09, -4.40] | -34.86 ms | -125.14 ms | Unavailable | +0.00% [+0.00, +0.00] |
| sparse | 10 | +0.96% [-2.64, +8.22] | -0.56% [-3.93, +3.87] | +0.97 ms | -1.91 ms | Unavailable | +0.00% [-0.16, +0.17] |

# Confirmation comparisons

Raw measurements and metadata: [`confirmation/`](./confirmation/).

## baseline-confirmation

| Scenario | Pairs | Throughput change [95% CI] | Engine CPU reduction [95% CI] | Wall saved / 1024 ticks | CPU saved / 1024 ticks | p99 change | Peak RSS change |
|---|---:|---|---|---:|---:|---:|---:|
| dense | 10 | +2.78% [+1.04, +3.66] | +2.85% [+1.03, +4.16] | +13.10 ms | +54.06 ms | +2.08% [-2.57, +5.95] | -0.27% [-0.95, +0.06] |
| multi | 30 | -1.10% [-6.09, +5.22] | +2.33% [+0.69, +3.37] | -44.49 ms | +300.24 ms | +2.01% [-9.77, +5.88] | +0.52% [-0.99, +1.78] |
| ai512 | 10 | +1.86% [+1.17, +2.93] | +2.15% [+1.69, +2.81] | +25.56 ms | +96.41 ms | +0.60% [-1.40, +6.76] | -0.61% [-1.70, +0.44] |
| disabled512 | 10 | +2.25% [+0.53, +3.75] | +2.38% [+0.98, +3.25] | +23.18 ms | +80.72 ms | -1.71% [-4.19, +4.38] | -0.28% [-0.98, -0.14] |
| sparse | 10 | +4.05% [+0.85, +8.36] | +2.87% [+0.31, +4.79] | +3.90 ms | +10.42 ms | -2.67% [-10.45, +2.13] | +0.00% [+0.00, +0.00] |
| saturated | 10 | +2.43% [+0.55, +3.80] | +2.43% [+1.04, +4.14] | +11.24 ms | +45.17 ms | +3.38% [+0.08, +7.47] | +0.15% [-0.18, +0.74] |
| harvested | 10 | +0.19% [-1.46, +0.92] | +0.72% [-0.93, +1.44] | +0.92 ms | +9.97 ms | +0.50% [-1.04, +1.59] | +0.11% [-0.44, +0.74] |
| fragmented | 10 | +1.36% [+0.42, +3.17] | +1.82% [+0.41, +3.11] | +36.78 ms | +189.16 ms | -2.54% [-4.39, +0.88] | +0.19% [-0.98, +1.48] |

## integration-confirmation

| Scenario | Pairs | Throughput change [95% CI] | Engine CPU reduction [95% CI] | Wall saved / 1024 ticks | CPU saved / 1024 ticks | p99 change | Peak RSS change |
|---|---:|---|---|---:|---:|---:|---:|
| dense | 30 | -2.95% [-4.08, -1.50] | -3.52% [-4.84, -1.40] | -13.82 ms | -61.30 ms | -0.62% [-2.96, +1.31] | +0.00% [+0.00, +1.50] |
| multi | 10 | +4.63% [+1.72, +7.69] | +0.14% [-1.12, +2.05] | +187.89 ms | +19.89 ms | +4.50% [-18.88, +38.20] | -0.46% [-2.44, +4.02] |
| ai512 | 30 | -1.49% [-1.94, -0.69] | +0.15% [-0.21, +1.16] | -20.77 ms | +6.62 ms | +15.95% [+12.11, +17.41] | +1.29% [+0.78, +1.79] |
| disabled512 | 10 | +0.82% [-1.04, +2.34] | +1.11% [+0.24, +2.44] | +8.53 ms | +37.48 ms | -1.32% [-2.68, +2.09] | +2.39% [+1.86, +3.34] |
| sparse | 10 | +0.16% [-1.07, +1.45] | +0.51% [-0.17, +1.14] | +0.15 ms | +1.79 ms | -4.14% [-9.77, +5.31] | +0.00% [+0.00, +0.00] |
| saturated | 30 | -2.99% [-4.23, -2.19] | -3.27% [-4.81, -2.30] | -13.79 ms | -56.71 ms | -0.03% [-2.04, +3.47] | +0.00% [+0.00, +1.52] |
| harvested | 30 | -0.31% [-0.49, +0.67] | +0.40% [+0.04, +0.55] | -1.51 ms | +5.25 ms | +0.53% [+0.17, +1.08] | +0.00% [+0.00, +1.92] |
| fragmented | 10 | -0.83% [-1.48, +1.21] | -1.07% [-2.00, +0.62] | -22.31 ms | -106.62 ms | -3.21% [-5.14, -0.79] | +2.58% [+0.49, +3.65] |

## latest-master

| Scenario | Pairs | Throughput change [95% CI] | Engine CPU reduction [95% CI] | Wall saved / 1024 ticks | CPU saved / 1024 ticks | p99 change | Peak RSS change |
|---|---:|---|---|---:|---:|---:|---:|
| dense | 10 | +4.44% [+1.73, +10.50] | +3.22% [+1.35, +9.26] | +21.61 ms | +63.62 ms | Unavailable | +6.20% [+5.29, +7.17] |
| multi | 10 | +45.24% [+35.80, +53.97] | +1.71% [+0.64, +4.97] | +1860.38 ms | +235.30 ms | Unavailable | +30.77% [+28.04, +34.98] |
| ai512 | 10 | -0.73% [-1.12, +0.09] | +0.83% [+0.04, +2.11] | -10.23 ms | +35.53 ms | Unavailable | +1.59% [+1.10, +2.39] |
| disabled512 | 10 | +2.67% [+0.81, +5.90] | +2.86% [+1.15, +5.19] | +27.06 ms | +96.46 ms | Unavailable | +2.72% [+2.01, +3.49] |
| sparse | 10 | +1.36% [-2.21, +3.88] | +2.50% [+0.12, +3.60] | +1.38 ms | +9.02 ms | Unavailable | +0.00% [+0.00, +0.00] |
| saturated | 10 | +8.72% [+5.98, +11.14] | +7.53% [+4.61, +9.28] | +40.09 ms | +146.59 ms | Unavailable | +5.78% [+5.02, +6.38] |
| harvested | 10 | +5.40% [+5.05, +6.54] | +4.39% [+4.13, +5.27] | +26.89 ms | +64.03 ms | Unavailable | +3.02% [+2.36, +3.87] |
| fragmented | 10 | +27.92% [+23.80, +30.42] | +6.76% [+2.43, +8.24] | +766.63 ms | +743.38 ms | Unavailable | +20.38% [+18.68, +23.09] |

## optimized-original

| Scenario | Pairs | Throughput change [95% CI] | Engine CPU reduction [95% CI] | Wall saved / 1024 ticks | CPU saved / 1024 ticks | p99 change | Peak RSS change |
|---|---:|---|---|---:|---:|---:|---:|
| dense | 30 | -1.09% [-1.98, +0.26] | -2.46% [-3.68, -1.00] | -5.02 ms | -43.45 ms | Unavailable | +0.00% [+0.00, +2.18] |
| multi | 30 | +3.19% [-0.49, +12.14] | -3.86% [-5.06, -1.01] | +128.94 ms | -464.85 ms | Unavailable | +10.01% [+8.86, +11.78] |
| ai512 | 10 | -0.63% [-1.12, +0.50] | -0.32% [-1.80, +1.06] | -8.94 ms | -13.77 ms | Unavailable | +2.17% [+1.91, +2.29] |
| disabled512 | 30 | -0.70% [-1.48, +1.17] | -0.13% [-1.89, +1.75] | -7.13 ms | -4.04 ms | Unavailable | +2.19% [+2.02, +2.40] |
| sparse | 10 | +1.15% [-0.70, +2.46] | +0.93% [+0.48, +1.88] | +1.10 ms | +3.34 ms | Unavailable | +0.00% [-0.21, +0.00] |
| saturated | 30 | -0.85% [-2.09, +0.71] | -2.33% [-3.63, -1.02] | -3.97 ms | -42.00 ms | Unavailable | +0.00% [+0.00, +2.00] |
| harvested | 30 | -1.19% [-1.72, -0.75] | +0.38% [-0.16, +0.74] | -5.79 ms | +4.97 ms | Unavailable | +0.00% [+0.00, +1.39] |
| fragmented | 10 | +0.56% [-1.07, +2.41] | -0.55% [-2.42, +1.98] | +14.89 ms | -58.70 ms | Unavailable | +1.66% [+0.09, +2.50] |

## direct-owner

| Scenario | Pairs | Throughput change [95% CI] | Engine CPU reduction [95% CI] | Wall saved / 1024 ticks | CPU saved / 1024 ticks | p99 change | Peak RSS change |
|---|---:|---|---|---:|---:|---:|---:|
| dense | 30 | -1.67% [-3.32, -0.60] | -3.24% [-4.95, -1.60] | -7.88 ms | -58.67 ms | -12.27% [-18.40, -8.72] | +0.00% [+0.00, +1.34] |
| multi | 30 | +9.49% [+5.89, +11.12] | -2.36% [-3.42, -0.42] | +373.54 ms | -289.94 ms | +30.26% [+24.03, +44.82] | +7.36% [+5.53, +9.18] |
| ai512 | 10 | +2.46% [+1.59, +3.16] | +0.59% [-0.39, +1.56] | +34.94 ms | +25.77 ms | +0.22% [-3.21, +2.51] | +2.17% [+1.68, +2.99] |
| disabled512 | 10 | -0.62% [-2.01, +1.00] | -1.12% [-2.43, +0.25] | -6.46 ms | -36.50 ms | -2.09% [-5.79, +1.52] | +1.79% [+1.21, +2.11] |

## merge-effect

| Scenario | Pairs | Throughput change [95% CI] | Engine CPU reduction [95% CI] | Wall saved / 1024 ticks | CPU saved / 1024 ticks | p99 change | Peak RSS change |
|---|---:|---|---|---:|---:|---:|---:|
| dense | 10 | -2.63% [-5.20, -0.44] | -3.46% [-5.77, -1.33] | -12.72 ms | -63.53 ms | -10.34% [-11.92, -4.30] | +4.61% [+3.82, +5.57] |
| multi | 10 | +0.21% [-2.84, +12.65] | +0.74% [-1.32, +5.02] | +7.24 ms | +102.17 ms | -16.69% [-28.24, -8.15] | -0.49% [-2.58, +2.04] |
| ai512 | 10 | -2.96% [-4.09, -0.75] | -0.23% [-0.94, +2.95] | -41.71 ms | -9.82 ms | +17.68% [+11.18, +20.26] | +1.88% [+1.19, +2.13] |
| disabled512 | 10 | -0.83% [-1.79, +0.97] | -0.68% [-2.71, +0.91] | -8.80 ms | -22.48 ms | +0.43% [-4.77, +8.13] | +2.23% [+1.80, +3.01] |

# Retained-Comparison comparisons

Raw measurements and metadata: [`retained-comparison/`](./retained-comparison/).

## retained-vs-edb

| Scenario | Pairs | Throughput change [95% CI] | Engine CPU reduction [95% CI] | Wall saved / 1024 ticks | CPU saved / 1024 ticks | p99 change | Peak RSS change |
|---|---:|---|---|---:|---:|---:|---:|
| dense | 10 | -0.62% [-2.01, +1.41] | -0.54% [-2.22, +1.53] | -3.00 ms | -10.18 ms | -4.83% [-6.39, -0.76] | -0.01% [-0.21, +0.41] |
| multi | 10 | -2.08% [-4.12, +3.37] | +2.10% [-0.62, +3.65] | -86.75 ms | +289.91 ms | -6.71% [-10.25, -2.77] | -0.28% [-3.76, +0.34] |
| ai512 | 10 | -1.53% [-2.70, +0.59] | -0.73% [-2.62, +2.12] | -21.85 ms | -31.98 ms | -2.40% [-5.69, +10.13] | +0.24% [-0.11, +1.10] |
| disabled512 | 10 | -0.44% [-3.62, +0.05] | -1.87% [-4.64, -0.37] | -4.61 ms | -62.28 ms | +1.31% [-3.74, +6.07] | -0.01% [-0.50, +0.25] |
| sparse | 10 | +4.56% [+2.94, +8.74] | +3.52% [+0.97, +5.45] | +4.52 ms | +13.16 ms | -8.42% [-16.85, +2.49] | +0.00% [+0.00, +0.00] |
| saturated | 10 | +1.82% [-1.43, +4.26] | +1.88% [-1.44, +3.95] | +8.15 ms | +34.47 ms | -2.31% [-9.05, +2.33] | +0.69% [+0.08, +0.88] |
| harvested | 10 | -1.88% [-3.11, -0.11] | -0.44% [-2.62, +0.45] | -9.02 ms | -5.90 ms | +4.08% [-0.79, +5.56] | +0.17% [-0.34, +0.71] |
| fragmented | 10 | -0.97% [-1.83, +1.02] | -0.88% [-1.80, +1.13] | -24.23 ms | -83.18 ms | -1.08% [-5.03, +1.60] | -0.56% [-1.85, +1.84] |

## retained-vs-latest-master

| Scenario | Pairs | Throughput change [95% CI] | Engine CPU reduction [95% CI] | Wall saved / 1024 ticks | CPU saved / 1024 ticks | p99 change | Peak RSS change |
|---|---:|---|---|---:|---:|---:|---:|
| dense | 10 | +8.73% [+6.51, +11.30] | +7.66% [+5.27, +9.72] | +41.26 ms | +154.05 ms | Unavailable | +1.17% [+0.50, +1.54] |
| multi | 10 | +58.25% [+47.67, +70.24] | +4.52% [+0.61, +7.29] | +2282.52 ms | +654.44 ms | Unavailable | +30.40% [+28.20, +33.59] |
| ai512 | 10 | +4.21% [+2.82, +4.96] | +2.83% [+1.41, +3.53] | +59.06 ms | +127.78 ms | Unavailable | -0.66% [-1.29, +0.17] |
| disabled512 | 10 | +2.01% [-1.64, +4.51] | +1.84% [-2.24, +4.27] | +20.68 ms | +61.81 ms | Unavailable | +0.27% [-0.21, +1.01] |
| sparse | 10 | -0.40% [-4.56, +1.93] | +0.83% [-0.62, +2.94] | -0.40 ms | +2.90 ms | Unavailable | +0.00% [+0.00, +0.00] |
| saturated | 10 | +6.84% [+4.03, +9.33] | +5.28% [+2.73, +7.53] | +30.71 ms | +99.48 ms | Unavailable | +0.18% [-0.42, +0.71] |
| harvested | 10 | +2.85% [+0.50, +5.70] | +3.75% [+0.73, +6.18] | +13.76 ms | +53.26 ms | Unavailable | -2.39% [-2.58, -1.75] |
| fragmented | 10 | +10.22% [+8.09, +13.70] | +5.32% [+2.29, +7.17] | +260.23 ms | +530.81 ms | Unavailable | +6.87% [+5.39, +8.36] |

# Interpretation and limitations

- Untouched master and optimized-original retain immediate growth. They differ from delayed growth in RNG, within-pass feedback, and publication timing. Their throughput comparisons are not same-work proofs; use the accompanying final stock, deposit, and growth-statistic records. Delayed variants must match checksums and all accepted-work counters.
- Optimized-original applies the applicable common A/C/D and contiguous-copy improvements to confirmation master 0f1a2569, retaining its immediate growth algorithm. B is specific to proposals.
- The initial all-off pilot changed layout and setter scaffolding, so its results are excluded. Corrected variants keep non-C layouts and B-off setter code unchanged. Pilot evidence and the reason for exclusion are retained.
- Stage wall timers overlap; never add snapshot, worker, queue, deadline-wait, and publication timers to infer total engine cost. Separate instrumented CPU attribution is diagnostic, not adoption evidence.
- Core isolation reserves physical cores and their SMT siblings. Shared memory/cache/package-power contention remains possible. Confirmation uses four physical cores; the planned timing sensitivity sweep was not run because no candidate qualified after integration.
- Snapshot allocation counts cover snapshot buffers only, not every heap allocation. Explicit copy bytes exclude allocator relocations and stamp metadata. Native retained-byte counters omit stock sidecars/stamps; OS peak RSS is used for memory acceptance.
- Untouched master has histogram tick percentiles rather than exact p99. No exact master p99 is fabricated. Master also lacks the extra branch world-checksum trace; replay traces are retained.
- Linux results do not establish Windows, macOS, Android, browser, or threadless determinism. These platforms were unavailable for execution in this experiment.

See [timer boundaries](./timing-boundaries.md) and the verification logs for detailed coverage.


## Final memory and baseline caveats

Against untouched current master, median peak RSS rises from189.2 to248.4MiB on multi-material (+30.4% paired median) and200.1 to213.1MiB on fragmented (+6.87%). Additional snapshot retention accounts for part of the difference: buffer high-water marks and reported retained capacity are recorded in `final-memory-investigation.json`. These counters omit stock sidecars and proposals, so they do not fully attribute RSS. This is an existing delayed-pipeline memory cost; no memory improvement is claimed. Untouched-master copied-byte accounting also omits material-stock sidecars that the branch counts, so the raw byte totals must not be directly compared as physical traffic.

The final same-behavior comparison against edb09d402 is mostly uncertain at ten pairs. Sparse throughput improves4.56% [2.94,8.74]; harvesting throughput falls1.88% [0.11,3.11], and disabled-growth CPU rises1.87% [0.37,4.64]. These are integration comparisons, not qualifying new optimizations, and were not extended to30. The intervals do not establish equivalence or rule out every2% regression. No candidate is adopted on their strength.
