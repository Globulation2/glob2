# Turret owner-cost profiling and simplification

Tested candidate: `68fd613617818692feb0bb9e99b9019860b147f8`; base/control implementation: `c96908b3ab4defd54ec363c5eab70ccf0d194d8b`, on master `277510840`. Source changes remain benchmark/shadow-only, shipping targeting and hiring unchanged. No live experiment is enabled.

## Changes justified by Callgrind

- Allocate a generation/team job only after selecting a source that needs refresh. Sort small building references, then reserve and construct the admitted sources once.
- Answer the tested cooldown forecast with an equivalent bounded multiplication/comparison instead of division. Tests cover actual cooldown boundaries, zero rhythm and large unsigned intervals.
- Reuse the complete immutable occupied index when its world/dimensions/chunk stamps are unchanged. Capture current entity combat facts independently.
- Store published records in a sorted contiguous vector instead of tree nodes. Use one lower-bound lookup for replacement. Maintain cleanup and source/target validation; account for retained spare capacity after removals. No epoch scheme or mutable shared query cache is added.

The snapshot projection itself contributes little to the profile: 14,256 instructions across 72 combat observations in the baseline, compared with 6.60 million for occupied-index construction. Removing projection would weaken lease discipline for little benefit. Full source cleanup still resolves retained references every tick; the change reduces traversal/storage overhead rather than eliminating liveness validation.

## Profiling protocol and findings

Dedicated release benchmark regions include initial admission, complete headless ticks and final drain, with setup/warmup/output/teardown excluded. Optional development-only Valgrind headers add no game dependency. `--instr-atstart=no --collect-atstart=yes` collects all executing threads within client-request regions. Three fixtures at one participant provide serial attribution; combat at four includes worker execution. Exact commands, binaries and dumps are archived. Callgrind time is not native performance.

Three implementations are compared: before, preparation only, and final preparation + publication. The first preparation attempt is also retained separately. Serial admission inclusive counts include index/derived facts; publication without complete subtracts its direct complete/join child, including observation destruction. That instruction measure differs from the native publication CPU scope, which excludes join CPU but includes subsequent teardown. Do not sum overlapping inclusive rows.

| Workload | Admission before → final instructions | Change | Publication excluding complete before → final | Change |
|---|---:|---:|---:|---:|
| combat | 9,501,104 → 8,787,285 | -7.51% | 2,753,427 → 2,294,975 | -16.65% |
| ordinary | 7,057,047 → 5,286,785 | -25.09% | 652,989 → 647,000 | -0.92% |
| empty | 18,616,705 → 14,638,434 | -21.37% | 11,839,866 → 11,161,072 | -5.73% |

Combat baseline publication performs 23,438 building resolutions (21,700 cleanup + 1,738 outputs); empty performs 43,400 (cleanup plus 21,700 refreshed outputs). Damage compatibility is smaller than index work/record traversal. The forecast function falls 1,239,620→772,132 combat instructions (−37.7%). These profiles support local changes, not a wholesale removal of snapshot lifetime/deadline safeguards.

## Native paired measurements

Linux x86_64, Ryzen7 6800H, GCC13.3 release `-O3`, no fast/PCH/unity. Participant count 4, capacity 8, K=R=1, margin=1. Mask 8,10,12,14; monitor on 0–7. Ten paired subprocess repetitions, rotating before/preparation/final order and each binary’s original/shadow/cached mode order. Each process warms each cell before its selected seed. Timing omits CPU probes; a separate identical campaign measures scopes. 270 timing observations and 270 CPU-probe observations. Do not pool with the earlier full-matrix protocol.

Before and preparation controls are relinked from archived old/intermediate targeting implementation objects with unchanged public headers and the same release compiler flags, remaining production objects and benchmark instrumentation. `control-provenance.json` and `preparation-control-provenance.json` record actual source inputs, commands and hashes. The common harness banner describes the candidate tree and must not be interpreted as the control’s implementation revision. Exact standalone source files and reconstruction/build scripts are included.

Accepted pairs: 20. Contended pairs retained and repeated: 4. A competing rendering release build occupied all cores during preflight; measurements waited. System/compiler overlap during a pair rejected the entire three-binary pair. No outlier was removed based on cost, no other process affinity was changed, and the optional reservation request was unnecessary once that build completed. Cooperative affinity does not isolate cache/bandwidth/power or desktop activity; all sampled activity/temperatures are retained.

| Phase | Workload | Cached µs/tick | Change vs previous prototype (95% CI) | Process CPU change (95% CI) |
|---|---|---:|---|---|
| before | empty100 | 203.69 | +0.00% [+0.00, +0.00] | +0.00% [+0.00, +0.00] |
| before | combat100 | 466.08 | +0.00% [+0.00, +0.00] | +0.00% [+0.00, +0.00] |
| before | ordinary4 | 171.08 | +0.00% [+0.00, +0.00] | +0.00% [+0.00, +0.00] |
| preparation | empty100 | 199.69 | -1.96% [-4.32, +0.52] | -1.22% [-2.54, +0.26] |
| preparation | combat100 | 464.06 | -0.43% [-1.25, +0.43] | -0.37% [-1.32, +0.61] |
| preparation | ordinary4 | 169.38 | -0.99% [-3.53, +0.92] | -0.61% [-1.95, +0.39] |
| after | empty100 | 198.51 | -2.54% [-4.06, -1.19] | -1.46% [-2.62, -0.22] |
| after | combat100 | 461.45 | -0.99% [-1.87, -0.16] | -1.00% [-1.71, -0.26] |
| after | ordinary4 | 172.07 | +0.58% [-1.78, +2.79] | +0.64% [-1.20, +2.69] |

The combined change improves empty mean ticks 2.54% and combat 0.99% versus the previous prototype; paired intervals support both. Preparation alone has whole-tick intervals crossing zero. Ordinary control’s +0.58% whole-tick/+0.64% process CPU changes versus the previous prototype are inconclusive; do not claim an ordinary whole-engine improvement. More efficient scopes do not necessarily reduce latency under shared scheduling.

## Owner and worker CPU scopes

Separate probe runs, µs/tick. The owner column includes incremental shared capture, admission/preparation/index, publication excluding joins and consumption; it omits planning executed by the owner at joins and unattributed executor bookkeeping. It is a measured scope sum, not an exact accounting of all owner CPU. The pipeline sum adds all planner callbacks on every executing thread. Incremental capture is a noisy paired difference against original in the same phase. Cheap firing probes have overhead.

| Workload | Owner scopes before → after | Pipeline scopes before → after | Original scans (after control) | Additional pipeline CPU |
|---|---:|---:|---:|---:|
| empty100 | 81.11 → 76.84 | 88.00 → 83.56 | 155.07 | -71.51 |
| combat100 | 16.45 → 14.40 | 21.53 → 19.46 | 9.21 | +10.25 |
| ordinary4 | 14.50 → 12.63 | 16.37 → 14.47 | 7.13 | +7.34 |

Combat publication falls 3.39→2.49 µs/tick (26.6%). Source/facts falls 3.99→3.63; combat index 2.86→2.80. In ordinary control, unchanged-index reuse reduces index 3.16→2.20 and publication 3.07→2.39. Owner scope sums fall about 12.5% combat and 12.9% ordinary; pipeline scopes fall 9.6% and 11.6%. Excluding the noisy incremental-capture difference, combat reductions are smaller (8.8% owner, 6.8% pipeline). Firing consumption is essentially unchanged; this pass optimizes production/publication, not ranking/validation semantics.

Against original targeting, the final prototype’s combat whole ticks are −0.27% (CI [−3.53,+2.10]), ordinary +0.63% ([−0.82,+2.15]), and empty −26.09% ([−26.90,−25.16]). Process CPU is empty −14.35%, combat −1.31% (interval crosses zero), ordinary +0.68% (interval crosses zero). The turret pipeline itself still adds approximately 10.25 µs/tick in combat and 7.34 in ordinary, versus saving 71.51 in empty. Improvements do not eliminate the extra targeting CPU.

## Correctness and unchanged behavior

- Committed-head focused engine checks: 77 cases, 326,475 assertions. Includes fresh HP/experience with shared unchanged index; overlapping generations with source upgrade and incarnation replacement; cooldown/overflow boundaries; custom/wrapped geometry, failure and shared lifecycle controls.
- Both control and candidate whole-engine tests: 2 cases/25,336 assertions each, including worker-count 1/2/4/8 determinism and original/shadow parity. Ten emitted checksum/order/save artifacts match byte-for-byte across the binaries.
- Committed-head stock Numbi/Castor actual-producer shadow corpus: 1 case/159,000 assertions, twenty matched battles; original/shadow traces/orders/final saves remain equal.
- The new overlap fixture initially left towers cooling while advancing only boundaries; 47 checks failed in that new case. It was corrected to start the fixture ready. The initial failed log and corrected passing runs are retained; no failing production case was hidden.
- Original/shadow continuation remains 9/9; cached continuation still fails cuts 39/40 and passes the other seven. Those failures remain live-readiness blockers. This pass adds no pipeline persistence or general match experiment.

Linux native coverage only. No supported-platform equivalence, future live replay/network/golden acceptance or changed-shot battle qualification is claimed. Native capacity4/participants1/2/8 qualification has not been repeated on this revision; their earlier failed/noisy cells are not waived by this focused four-participant study. Keep the first-valid rule, four-tick budget, shipping targeting and hiring unchanged.

## Recommendation

Keep these measured local simplifications. They reduce preparatory/publication work and deliver a small combat improvement without changing tested trajectories. Do not infer that the entire snapshot system is the bottleneck: index work and lifecycle validation dominate the relevant profile, while projection is small. Total turret CPU remains above original in combat/control, and ordinary latency has no demonstrated improvement. Continue qualification only after addressing the known continuation and remaining coverage gates.
