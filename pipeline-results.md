# Delayed gradient pipeline experiments

Opt-in headless prototype; ordinary scheduling remains unchanged. One periodic field is seeded per tick and published at a fixed deadline. Worker count changes execution only; delay changes simulation behavior. Zero workers at each delay is the serial control.

**Host:** Apple M3, 4 performance + 4 efficiency cores, 24 GB RAM. Other user-owned simulation workloads ran concurrently. Paired/rotated repetitions reduce drift but do not establish a universal optimum or an idle-host performance result. Our own builds and correctness runs did not overlap timing.

Wall time includes process setup and teardown. CPU is whole-process user + system time across all threads. Outstanding jobs are completed before timing ends, without early publication. AI groups have equal weight; the small control is excluded from heavy aggregates.

## Broad sweep

Six retained scenarios, one warmup plus two measured repetitions per configuration. Seeds and exact commands are in the metadata and JSONL rows. Speedup and CPU ratios below compare with the same-delay zero-worker control.

| Delay | Background workers | Wall speedup | CPU ratio | Worst heavy CPU ratio |
|---:|---:|---:|---:|---:|
| 1 | 1 | 1.026× | 1.009× | 1.026× |
| 1 | 2 | 1.033× | 1.002× | 1.011× |
| 1 | 4 | 1.035× | 0.999× | 1.013× |
| 1 | 8 | 1.026× | 0.986× | 1.004× |
| 3 | 1 | 1.187× | 1.024× | 1.045× |
| 3 | 2 | 1.221× | 1.026× | 1.062× |
| 3 | 4 | 1.208× | 1.038× | 1.067× |
| 3 | 8 | 1.197× | 1.039× | 1.064× |
| 8 | 1 | 1.252× | 1.024× | 1.048× |
| 8 | 2 | 1.247× | 1.044× | 1.088× |
| 8 | 4 | 1.259× | 1.038× | 1.066× |
| 8 | 8 | 1.246× | 1.049× | 1.085× |

## Five-repeat confirmation: three-tick delay

One warmup plus five measured repetitions. Values are medians; worker count excludes the main simulation thread.

| Scenario | Serial wall | 1 worker | 2 workers | 2-worker speedup | CPU change | 2-worker wait |
|---|---:|---:|---:|---:|---:|---:|
| land-4-1001-maxima-late | 2.430s | 2.141s | 2.079s | 1.169× | +0.9% | 0.045s |
| water-4-1001-cortex-late | 3.102s | 2.610s | 2.615s | 1.186× | -0.5% | 0.008s |
| water-4-1002-nicowar-late | 4.532s | 3.535s | 3.366s | 1.346× | +3.0% | 0.141s |
| land-2-1002-mixed-middle | 0.709s | 0.646s | 0.649s | 1.092× | +1.8% | 0.001s |
| water-8-1003-nicowar-middle | 8.900s | 7.102s | 6.947s | 1.281× | +2.5% | 0.020s |
| water-4-1003-mixed-middle | 3.257s | 2.636s | 2.614s | 1.246× | +2.4% | 0.147s |

d3-w1: equal-AI aggregate **1.206× wall speedup**, **+0.6% CPU**; worst heavy CPU change **+2.5%**. Versus the original engine: 1.202× wall speedup and +0.9% CPU (different simulation schedule).

d3-w2: equal-AI aggregate **1.227× wall speedup**, **+1.4% CPU**; worst heavy CPU change **+3.0%**. Versus the original engine: 1.223× wall speedup and +1.7% CPU (different simulation schedule).

## Full-game check

128×128 land, two Nicowars, map seed 1004 and game seed 19. One warmup plus five measured repetitions. Compare threading only within the delayed schedule; legacy games may end on a different tick.

| Configuration | Median wall | Median CPU | End tick(s) | Termination |
|---|---:|---:|---|---|
| d3-w0 | 6.700s | 6.696s | [82402] | engine_end |
| d3-w1 | 5.311s | 7.017s | [82402] | engine_end |
| d3-w2 | 5.089s | 7.196s | [82402] | engine_end |
| legacy | 12.912s | 12.900s | [134754] | engine_end |

## Interpretation and limits

- One or two background workers capture most of the available overlap. Four/eight workers offer no consistent improvement in this sweep. Three ticks is a conservative tested delay; eight ticks has a small screening advantage on some workloads and has not received the same five-repeat confirmation.
- The 1.5× aggregate wall-speed target is not met. Low deadline wait with two workers points to remaining main-thread work as the next bottleneck.
- Same-delay worker variants produced identical final outcomes and scheduling counters in every retained timing run. Exact per-tick trace comparisons also passed independently on mixed, homogeneous and 512×512 fixtures, and across all 82,402 ticks of the complete two-Nicowar game (zero versus two workers).
- Scheduler ThreadSanitizer, injected creation/work failures, bounded storage, deterministic publication, stale-result supersession and teardown tests passed. Real-engine tests cover captured terrain and synchronous resource/guard/clear refreshes.
- Pipeline-disabled replay bytes, final-save bytes, per-tick traces and save continuation still match the original baseline.
- Pipeline save/replay exports are deliberately rejected. Versioned pending-job persistence and replay/network semantics are required before production use. Normal save formats remain unchanged.
- Linux/Windows CI coverage is wired but was not executed here. Cross-platform checksum equivalence and full-engine race-sanitizer coverage are not established; the earlier full-engine sanitizer attempt failed in SDL initialization before main.

## Evidence

- [pipeline-screen](/Users/bradley/glob2/artifacts/parallel-compute/pipeline-screen)
- [pipeline-confirm](/Users/bradley/glob2/artifacts/parallel-compute/pipeline-confirm)
- [pipeline-full](/Users/bradley/glob2/artifacts/parallel-compute/pipeline-full)
- [pipeline-final-check](/Users/bradley/glob2/artifacts/parallel-compute/pipeline-final-check)
- [pipeline-extra-verify](/Users/bradley/glob2/artifacts/parallel-compute/pipeline-extra-verify)
- [pipeline-full-verify](/Users/bradley/glob2/artifacts/parallel-compute/pipeline-full-verify)
- [pipeline-disabled-check](/Users/bradley/glob2/artifacts/parallel-compute/pipeline-disabled-check)
- [pipeline-tsan-final.log](/Users/bradley/glob2/artifacts/parallel-compute/pipeline-tsan-final.log)
- [pipeline-confirm-integration.log](/Users/bradley/glob2/artifacts/parallel-compute/pipeline-confirm-integration.log)
- [pipeline-confirm-scheduler.log](/Users/bradley/glob2/artifacts/parallel-compute/pipeline-confirm-scheduler.log)
- [pipeline-path.log](/Users/bradley/glob2/artifacts/parallel-compute/pipeline-path.log)
- [pipeline-full-manifest.json](/Users/bradley/glob2/artifacts/parallel-compute/pipeline-full-manifest.json)
- [pipeline-screen-manifest.json](/Users/bradley/glob2/artifacts/parallel-compute/pipeline-screen-manifest.json)

Repeated timing commands are retained in `run-pipeline-confirm.sh`; each timing directory includes binary/input hashes, raw commands, measurements and reviewed summary JSON.

Example invocation:

```sh
build/darwin/client/release/src/glob2 --run-game --load-game /path/to/checkpoint.game \
  --ticks 90000 --gradient-workers 2 --gradient-delay 3 --output-dir /absolute/output
```
