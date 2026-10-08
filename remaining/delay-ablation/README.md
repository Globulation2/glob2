# Player-free delay ablation

20 matched seed pairs; 30,000 percentile bootstrap resamples, RNG seed 713; percentage=ratio of mean final stocks minus 1. Intervals are pointwise. Primary exact two-sided sign tests use Holm across 36 delay-vs8 contrasts, testing directional consistency. Flush/reference contrasts exploratory. Owner/shared are not independent replicates.

Only publication delay changes in the new pipeline. 20 seeds, 512 ticks, 64² maps, six scenarios, no player seats/units/buildings/harvesting. Each delay has 360 runs including repeated immediate reference controls; 2,520 runs total. Owner/shared exact per-tick checksums/statistics pass; report stocks, checkpoints and terminal flush also match. Starting inputs and immediate reference outputs match across all delays. No performance claim.

## Final stock relative to immediate reference (%)

| Delay | Dense | Sparse | Multi | Saturated | Blocked | Disabled |
|---|---:|---:|---:|---:|---:|---:|
| 1 | -0.72% | -3.46% | -13.86% | -1.13% | -0.01% | +0.00% |
| 2 | -1.06% | -3.87% | -14.24% | -1.26% | -0.05% | +0.00% |
| 3 | -1.47% | -4.22% | -14.70% | -1.48% | -0.09% | +0.00% |
| 4 | -1.61% | -4.55% | -14.93% | -1.78% | -0.12% | +0.00% |
| 8 | -2.91% | -5.84% | -16.69% | -2.32% | -0.24% | +0.00% |
| 12 | -3.83% | -7.20% | -17.84% | -3.15% | -0.47% | +0.00% |
| 16 | -4.90% | -8.14% | -19.42% | -3.81% | -0.55% | +0.00% |

## vs_delay8: paired percentage differences [pointwise 95% CI]

| Delay | Dense | Sparse | Multi | Saturated | Blocked | Disabled |
|---|---:|---:|---:|---:|---:|---:|
| 1 | +2.25 [+1.87,+2.64] | +2.53 [+2.06,+3.01] | +3.40 [+2.80,+4.00] | +1.22 [+0.85,+1.61] | +0.24 [+0.15,+0.33] | +0.00 [+0.00,+0.00] |
| 2 | +1.90 [+1.51,+2.31] | +2.09 [+1.60,+2.58] | +2.94 [+2.28,+3.60] | +1.08 [+0.77,+1.42] | +0.20 [+0.12,+0.29] | +0.00 [+0.00,+0.00] |
| 3 | +1.48 [+1.09,+1.88] | +1.72 [+1.28,+2.16] | +2.38 [+1.94,+2.83] | +0.86 [+0.54,+1.19] | +0.16 [+0.08,+0.24] | +0.00 [+0.00,+0.00] |
| 4 | +1.34 [+0.99,+1.70] | +1.37 [+0.99,+1.76] | +2.12 [+1.70,+2.54] | +0.55 [+0.18,+0.91] | +0.13 [+0.06,+0.20] | +0.00 [+0.00,+0.00] |
| 8 | +0.00 [+0.00,+0.00] | +0.00 [+0.00,+0.00] | +0.00 [+0.00,+0.00] | +0.00 [+0.00,+0.00] | +0.00 [+0.00,+0.00] | +0.00 [+0.00,+0.00] |
| 12 | -0.95 [-1.26,-0.71] | -1.45 [-1.97,-0.97] | -1.38 [-1.90,-0.91] | -0.86 [-1.09,-0.62] | -0.23 [-0.30,-0.15] | +0.00 [+0.00,+0.00] |
| 16 | -2.06 [-2.50,-1.60] | -2.44 [-2.93,-1.95] | -3.28 [-3.75,-2.86] | -1.53 [-1.89,-1.16] | -0.30 [-0.41,-0.20] | +0.00 [+0.00,+0.00] |

## flushed_vs_delay8: paired percentage differences [pointwise 95% CI]

| Delay | Dense | Sparse | Multi | Saturated | Blocked | Disabled |
|---|---:|---:|---:|---:|---:|---:|
| 1 | +1.19 [+0.82,+1.56] | +1.09 [+0.71,+1.47] | +1.81 [+1.22,+2.41] | +0.43 [+0.06,+0.83] | -0.08 [-0.14,-0.01] | +0.00 [+0.00,+0.00] |
| 2 | +0.99 [+0.61,+1.38] | +0.92 [+0.54,+1.31] | +1.56 [+0.91,+2.23] | +0.41 [+0.09,+0.74] | -0.07 [-0.13,-0.01] | +0.00 [+0.00,+0.00] |
| 3 | +0.74 [+0.37,+1.11] | +0.81 [+0.47,+1.17] | +1.24 [+0.78,+1.70] | +0.30 [-0.01,+0.63] | -0.07 [-0.12,-0.01] | +0.00 [+0.00,+0.00] |
| 4 | +0.77 [+0.43,+1.10] | +0.54 [+0.26,+0.84] | +1.24 [+0.83,+1.66] | +0.10 [-0.27,+0.45] | -0.05 [-0.11,+0.01] | +0.00 [+0.00,+0.00] |
| 8 | +0.00 [+0.00,+0.00] | +0.00 [+0.00,+0.00] | +0.00 [+0.00,+0.00] | +0.00 [+0.00,+0.00] | +0.00 [+0.00,+0.00] | +0.00 [+0.00,+0.00] |
| 12 | -0.30 [-0.61,-0.06] | -0.91 [-1.36,-0.47] | -0.53 [-1.07,-0.03] | -0.33 [-0.57,-0.09] | +0.03 [-0.00,+0.06] | +0.00 [+0.00,+0.00] |
| 16 | -0.89 [-1.35,-0.42] | -1.25 [-1.68,-0.83] | -1.62 [-2.09,-1.20] | -0.56 [-0.92,-0.20] | +0.09 [+0.02,+0.15] | +0.00 [+0.00,+0.00] |

The terminal flush applies existing proposals in order without calculating further batches; it is diagnostic, not an additional normal simulation period. Normal endpoints do not publish early. It removes the final queue tail but cannot undo earlier delays in seeding, reproduction or snapshot feedback.

This is the retained immediate reference, not the latest master executable. Eight-tick historical results must reproduce exactly. Fixed fixtures and 20 seeds do not establish effects on arbitrary maps or long-run equilibrium. Linux x86-64 only; non-Linux/threadless unavailable. Heavy checksums join workers each tick, so this is not throughput or overlapping-compute evidence.

Reproduction: build the release engine test binary using build.log, then run run-growth-delay-ablation.py and analyze-growth-delay-ablation.py from the repository root. The manifest and source patch identify inputs/binary/source.

Tested commit `a4cf15104d437b2d4ef86aa60c200e4b3c30f458`. Integrated base `67fd5b935c98c3d6d0c5ba9d25754ce7df0f83f9`. Fetched master `607d2d06fc6abd1756acc5f0761fc78fecd9058a`; its newer vertex-terrain/save changes remain unintegrated, as recorded in the PR. This experiment changes only the test harness and documentation; production growth and default delay remain unchanged.
