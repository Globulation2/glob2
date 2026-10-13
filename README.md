# CPU batch efficiency verification

Baseline: master `3e2ba6c07450c94ce0f997220479fd309b71e461`. Measured candidate: `520d0b61280a5aa9d13f5b269770819dcb5b569f`, PR #1160. Final integration candidate: `a36aed67e3ae756c47afd8c398743379191d4ec3`, rebased onto master `26715e41b` after its shared cell-rule header changed. Rendering changes are byte-identical; timings above belong to the original measured endpoints, while final integration checks are recorded separately.

The change reuses immutable requested source regions, packs disjoint raster tiles into jobs, writes terrain directly into unpublished cache pages, and doubles bounded cell groups without increasing the 16K-sample terrain callback limit. Immediate rendering remains the default. Automatic path selection is deferred.

## Correctness

Final integration on `a36aed67e3ae756c47afd8c398743379191d4ec3` passes **97 unit cases and 123 isolated engine cases** after refreshing the shared scene/cell-rule boundary. Scene-buffer and scene-extraction checks are included. The measured candidate previously passed 95 unit/97 engine cases; those logs remain separately identified. Rendering changed files are byte-identical after the rebase.

- 95 native unit cases and 97 isolated engine integration cases pass at the candidate revision. These include cache revisions/crops/aliases/pressure, owner ledger restoration, worker scheduling, renderer admission/fallback, resizing, hardware/CPU terrain parity, high-resolution artwork and save/load continuity.
- Standalone ASan/UBSan production raster/executor stress passes with 420 actual native-worker callbacks, 20-frame source reuse, 50 worker failure/recovery cycles, partial-source gradients, clipping/alpha, pending mutation, source destruction and exact pixel/byte accounting.
- All 44 retained BMP captures are byte-identical across baseline/current immediate/parallel for 11 one-frame workloads. Captures include 1280×800 and requested 1920×1080 (actual 1854×1011 due window-manager clamping). These prove captured frame parity, not multi-frame visual smoothness. Frozen simulation checksum 2231453127 is unchanged.

## Environment and builds

Linux x86_64, GCC 13.3.0, SDL 3.4.16, Radeon 680M, native X11 software renderer. Development validation uses `python3 tools/dev_build.py -j8 unit-tests engine-tests` (-O0 -g1). Performance uses ordinary `scons release=1 -j8 software-render-benchmark` (-O3 stripped release), not fast-development artifacts. Runtime dependencies remain unchanged. Existing pinned SDL and dependency prefixes are listed in exact build logs.

The baseline adds only identical draw-plus-presentation frame statistics via `baseline-harness.patch`; production rendering code is unchanged. Candidate harness also reports source/job counters and optional trace data; formal frozen-scene measurements leave trace off, never use the interactive mode and do not substitute thread CPU clocks. Saved release executables and hashes are retained. A separate 32-cell control differs from candidate only by `candidate32-control.patch`.

All endpoints read the exact same retained fixture and runtime asset snapshot. `runtime-assets-manifest.json` records 7470 files. It is an exported WebP package, not a plain Git tree; reproduce with the pinned package exporter from source revision 2d5cf67 using `python3 tools/package_assets.py --platform linux --output <asset-directory>` and verify hashes. The fixture is retained in the archive; fixture SHA256 `26cc515a843c418ccc6af6d024398ee5ab1c53635cd881d3da4`.

## Performance measurements

Seven rotated/reversed matched release repeats, 308 formal processes across 11 workloads and four endpoint/mode controls. No compiler, engine or test-runner activity was sampled before/after retained trials; unrelated loaded attempts are separate and excluded. Full-frame statistics include presentation/final flush. CPU includes all process threads. Medians of per-run medians and medians of paired ratios are reported; percentiles are not pooled.

| Workload | Old parallel ms | New parallel ms | Paired frame change | Paired CPU change | New parallel vs immediate frame |
|---|---:|---:|---:|---:|---:|
| animated-1080 | 5.979 | 5.948 | -0.7% | +3.1% | -5.9% |
| animated-warm | 3.457 | 3.301 | -1.9% | +0.8% | -4.4% |
| cold-first-frame | 61.179 | 60.563 | -1.0% | -1.4% | -11.4% |
| gui-double-pan | 7.009 | 6.862 | -2.0% | -4.7% | -1.4% |
| gui-fractional | 5.992 | 5.855 | -1.9% | -4.0% | +4.3% |
| gui-half-animated | 6.072 | 5.983 | -1.5% | -3.6% | +7.1% |
| gui-half-pan | 5.930 | 5.850 | -1.2% | -3.2% | +6.0% |
| gui-half-pan-1080 | 10.831 | 10.693 | -1.4% | -2.8% | +5.0% |
| gui-native-pan | 4.361 | 4.388 | +0.1% | +0.3% | +0.3% |
| static-warm | 1.710 | 1.692 | -7.1% | -8.1% | -3.9% |
| streamed-pages | 14.576 | 14.216 | -2.7% | -2.0% | -33.4% |

Negative changes mean less time or CPU. Effects are modest: warm animated/map and GUI wins are generally 1–3%, with 3–5% less CPU in several GUI workloads; warm animated CPU is 0.8% higher and larger animated CPU 3.1% higher. The static-warm 7.1% change is not attributed to batching: warmed native static frames perform no new terrain/raster jobs. Small whole-frame differences include unmodified work, code layout, driver/system variation; paired distributions and immediate controls are retained rather than presenting all changes as parallel kernel gains.

Parallel still costs 18.9% more CPU than candidate immediate in animated 1280×800 scenes despite 4.4% lower frame time. Half-scale GUI remains 6.0% slower/9.8% more CPU; half-scale animated GUI 7.1% slower/11.8% more CPU. Uncached terrain remains 33.4% faster/14.5% more CPU; first cold terrain 11.4% faster/4.2% more CPU. These latter comparisons describe workload suitability, not gains newly created by this PR. Keep immediate default; no automatic-selection policy is introduced.

All matched cache hits/rebuilds agree. Raster painted tile/reference inventories agree; command packing reduces jobs about 88–90% in the affected GUI cases. Copied source bytes fall 11.0% for half-scale GUI panning, 19.2% for its larger framebuffer, 9.5% for fractional GUI and 91.3% for double-scale panning. Animated half-scale sources change every frame, so reuse does not reduce copied bytes there. Frame/CPU gains are much smaller than these structural reductions because other drawing work remains.

The separate 42-process control compares final source differing only by terrain group cap 32 vs 64. Seven alternating pairs give:

| Workload |64 vs 32 frame |64 vs 32 CPU |
|---|---:|---:|
| animated-warm | -6.2% | -5.0% |
| cold-first-frame | +0.9% | -0.2% |
| streamed-pages | +0.3% | -0.7% |

This supports retaining 64-cell groups for moderately changed terrain: the animated workload benefits, while cold/streamed differences are small. Callback sample limit remains 16K; group storage still obeys reservation/headroom limits.

Structural pilots ran under build load and are not timing evidence: 64 cells changes animated batches 240->120 with identical 360 jobs/5310 cells over 30 frames. The initial whole-image cache increased GUI copying by 25%; it was replaced by requested-region reuse before the final candidate. Raw optional traces are retained only as diagnostics, not isolated callback/gameplay performance claims.

## Coverage limits

Other operating systems/architectures, serial/threaded browsers, full hosted development verification and maintainer playtesting are not covered. This rendering change adds no simulation execution, save-format, replay/network or sim-version change. No isolated interactive gameplay-latency or GPU speed claim is made. Packing preserves logical reference/sample limits, not a guarantee of identical callback wall time. Optional raw traces are diagnostic; their pilot durations ran under compiler contention and are labelled accordingly.

## Archive

Identical capture bytes are deduplicated: one BMP/PNG per workload represents all four matching hashes; all 44 process logs and the capture manifest remain. Original measured release executables are retained unchanged. If the archive is split, concatenate numbered `verification.tar.gz.part*` files in order, verify `SHA256SUMS`, then extract. This keeps individual evidence files below GitHub's 100 MiB limit.

Download [archive part 1](verification.tar.gz.part1), [part 2](verification.tar.gz.part2) and [checksums](SHA256SUMS). Concatenate the parts in order to reconstruct `verification.tar.gz`.
