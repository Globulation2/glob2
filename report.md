# Parallel GPU gradient pipeline

Local review evidence for uncommitted changes on `codex/gpu-gradient-optimization`.

Original simulation workers now own independent lazy GPU execution lanes. Each lane has an in-order command queue, private kernel instances, gradient buffers, masks, descriptors and packing scratch. Terrain preparation, uploads, propagation, convergence checks and readback can proceed independently of other lanes. No additional worker pool or GPU actor was added. A lane still waits for its own results before returning through the existing gradient scheduling/publication path.

The context and six compiled programs are shared. The eight-entry terrain cache contains immutable GPU planes. Readiness futures prevent consumers from reading an incomplete upload; retained leases prevent cache eviction from destroying in-flight buffers. Short cache locks only reserve, publish or touch entries; preparation and content comparisons run outside them. Per-class calibration locks avoid duplicate first-use benchmarks while allowing unrelated classes to calibrate independently. Backend/parameter selection remains permanent per game and family/actual batch size, except permanent CPU fallback on errors. Failure quarantine cancels other lanes before committing staged results.

## Verification

- 76 focused unit cases, including 17 OpenCL cases: [unit log](unit.log), [JUnit](unit-junit.xml).
- 58 focused engine cases: [engine log](engine.log), [JUnit](engine-junit.xml).
- 32 engine runs with exact per-tick traces, replay bytes, final saves and cross-backend checkpoint continuations, 128²/256² maps and 1/4 threads: [log](exact.log), [commands/hashes](engine-exact/runs.json).
- Four injected failure harnesses, including failures during batches, late propagation, calibration and tuning: [log](errors.log). Reproduction script: `../optimization-team/verify-errors.py`.
- Android-disabled OpenCL implementation compiled and assertion harness passed. Other platform/GPU execution is unverified; native GPU tests ran on Linux x86_64, GCC 15.2, NVIDIA RTX 2070 SUPER, driver 580.178.04.
- A held terrain callback does not block another lane computing a different identity. Twelve concurrent callers across mixed dimensions, costs and obstacles match the CPU oracle. Twelve same-class callers calibrate once. These establish host pipeline independence; device scheduling ultimately controls actual kernel overlap.

## Build and integration

Source/base/binary hashes: [manifest](manifest.json). Final build log: [final-build.log](final-build.log). Master was fetched; the newer base changes only release workflow/tooling files, so no further rebase was needed. Numerical behavior and publication deadlines are preserved; no save, replay or simulation-version format change is intended.

```sh
GLOB2_SDL3_PREFIX=/home/bradley/glob2-claude/build/sdl3/prefix \
GLOB2_RECORDING_PREFIX=/home/bradley/glob2-vertex-terrain/build/linux/client/release/recording/prefix \
CCACHE=1 scons release=1 server=0 linker=auto -j16 \
build/linux/client/release/dev-linker-auto/src/glob2 unit-tests engine-tests

flock artifacts/gpu-gradients/optimization-team/gpu-benchmark.lock \
python3 artifacts/gpu-gradients/parallel-lanes/compare.py
```

Benchmark results and diagnostic profiling will be appended after completion. All match timings use the previous fixtures: seed 4242, game seed 19, four AIs, four original workers, 20,000 ticks. Wall time includes first-use calibration and the 1,000 warmup ticks; loading/final-save work is outside the measured run. These are match segments, not complete games. The three-repeat mode order rotates to reduce ordering bias.

## Whole-match segment performance

Three rotated repeats per mode/map; all 18 runs match original CPU checksum and final save bytes.

| Map | CPU | Previous serial GPU | Parallel GPU | Parallel automatic |
|---|---:|---:|---:|---:|
| 512² | 36.109 s | 42.627 s | 34.929 s | 34.987 s |
| 256² | 12.607 s | 16.162 s | 13.676 s | 13.498 s |

Parallel forced GPU is 3.3% faster than CPU on 512², and 8.5% slower on 256². Compared with the preceding serial GPU campaign, elapsed time fell 18.1% and 15.4%; those earlier medians came from a separate campaign rather than interleaved old/new repeats. Automatic is 3.1% faster on 512² and 7.1% slower on 256².

[Raw measurements, commands and hashes](final-match-results.json), [log](compare.log), [previous serial campaign](../optimization-team/final-match-results.json). Results apply to these fixtures and hardware; they do not establish a universal GPU win.

## Pipeline profile and device overlap

Separate host-clock diagnostic: 512², four original workers, 20,000 ticks, **35.187s** with exact CPU checksum/save. Four execution lanes were observed; steady compute intervals overlapped for 8.710s, with maximum four simultaneous host calls. Their 28.620s summed duration overlaps across lanes and is not serialized match time.

Shares of summed GPU-call host wall: packing/cost preparation/readiness 2.5%, initial uploads 13.0%, argument setup 1.8%, propagation/convergence 73.7%, output readback 9.0%. Shared cost lookup/preparation/readiness totaled 0.029s, including 0.006s readiness waiting. Only two terrain uploads, totaling 2MiB, occurred; seed/output traffic remained about 6.818GB each. The previous serialized diagnostic spent36.5% in packing; immutable per-plane reuse and independent lanes removed that dominant host phase. Different tuning selections and overlap prevent treating phase differences as controlled device-only gains.

The separate2,000-tick sampled OpenCL event diagnostic collected4,776 kernel events on four lanes. Driver start/end timestamps show maximum **two concurrent kernel commands**, with0.442ms overlapping intervals out of164.483ms union. No in-order queue violations occurred. Final checksum/save matched the equivalent CPU run. This demonstrates actual driver-reported device command overlap, not GPU ALU utilization. There are still data dependencies within each field and driver/hardware scheduling limits; adding queues does not imply unrestricted GPU execution.

[Host profile and limitations](../optimization-team/parallel-host-profile/report.md), [host metadata/commands/hashes](../optimization-team/parallel-host-profile/metadata.json), [device overlap report](../optimization-team/parallel-event-profile/report.md), [device metadata](../optimization-team/parallel-event-profile/metadata.json). These instrumented runs are separate from the performance medians. Production source hashes were rechecked unchanged after profiling.

## PR source revision

Tested commit `0003dbef349776f70d7f798d8c2ba81498398507`: the source hashes and release binary are identical to the validated working tree. Current master `672db67d2f11a7549413181f55b864b7c884709b` adds shared dependency staging for paths containing spaces and release workflow changes. This build uses explicit pre-existing SDL/recording prefixes, so the dependency staging path is bypassed; engine source and dependency binaries are unchanged. The PR is draft; platform omissions remain explicit.
