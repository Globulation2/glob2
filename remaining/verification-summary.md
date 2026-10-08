# Correctness and compatibility evidence

The experiments intentionally preserve simulation behavior, RNG draws, proposal order, publication timing, stock values, source counts, statistics and invalidation behavior. Dirty-generation numbers may differ. No save/replay/network/simulation revision change is associated with candidates A–D.

## Frozen experimental builds

- Corrected independent variants: seven builds × 43 focused cases, 950,711 assertions per build.
- Differential engine matrix: 864 run records across eight fixtures, delays 1/3/8, owner execution and shared thread counts 1/2/4/8. Per-tick world and replay checksums match the archived baseline. See `verification/results.json` and its traces.
- Stripped all-four production candidate: 43 focused cases; 144 matrix records including reused baseline references; 153 broader passes with five display skips; 12 golden passes. See `production-verification/`, `production-broad.xml`, `production-golden.xml`.
- Refreshed-master integration baseline and optimized candidate: each has 144 matrix records matching the old baseline traces. See `integration-baseline-verification/` and `integration-production-verification/`.
- Integrated optimized candidate: 198 broader passes, eight display skips; 12 golden passes; nine executor unit cases. See `integration-broad.xml`, `integration-golden.xml`, `integration-executor.xml`.
- Direct-owner controls: 12 focused resource-growth cases plus owner continuation traces; all match baseline. See `integration-direct-verification/` and `extra-verification/`.
- Optimized immediate-growth master: 57 focused passes in proper isolated registry execution. Its replay traces match untouched master at thread counts 1/2/4/8. The original failed combined-process invocation is retained: it used an obsolete copy-count expectation and combined tests that require process isolation. The corrected invocation did not regenerate goldens or change simulation logic.

The focused suites cover pending-batch save/load, migrated legacy pending masks, exact deadlines, paused boundaries, source removal and replacement, capacity/zero crossings, independent material stocks, statistics, slot reuse/rebuild, retained snapshots, catalog replacement, map transformations, shared resource/gradient state, session snapshots and executor lifecycle. Legacy save fixtures include versions 84, 88 and observation format 139. Test names, commands, logs and traces are preserved rather than treating this prose as proof.

Whole-engine timing runs separately assert final checksum and sampled/proposed/accepted/rejected/clamped/stock/tile counters for every same-behavior comparison. Saving/checksum runs are separate from primary timing, and pending calculation is drained at shutdown. Untouched master does not expose the branch's extra world-checksum trace; master comparisons use its replay trace and final resource/statistic records.

## Limits

Execution coverage is Linux x86-64/GCC 15.2 only. Windows, macOS, Android, browser and threadless runtime checks were unavailable; Linux agreement is not cross-platform determinism evidence. Display cases were skipped in the headless environment. All verification used locally resolved dependencies and release flags recorded in the build metadata. Hosted cheap checks are not represented as engine verification.

Final retained-branch validation is recorded separately after restoring the unoptimized implementation and resolving the master integration; experimental candidate passes do not substitute for testing the delivered revision.

## Delivered integration

Commit `9e9497d7151effae8fdd00a48db515511d30077a`, tree `85e5dffafb8c4a87b5fc8946fc96bbc6f4970e70`, integrates master `6487b873dd3f29ad3ab75c7513597fa47905c132`. A–D are absent. The retained executable hash and clean tree are frozen in `retained-freeze.json`.

- 210 broader engine cases passed; ten display-dependent cases skipped. See `retained-broad.xml` and command/log files.
- 21 unit cases across five suites passed, including nine executor cases. See `retained-unit.xml`.
- All 12 golden cases passed after the required version integration. The seeded-resource fixture has identical resource digests in all 150 rows; each checksum differs by exactly the save-header contribution (XOR 100663296). The match record was regenerated because its version gate changed. Initial failures, update commands, final passes and the sim-version check are preserved.
- The retained implementation's 144 differential records match edb09d402 per tick across all eight scenarios, delays 1/3/8, owner/shared and 1/2/4/8 threads. See `retained-verification/results.json`.
- New maintained fixtures test both incompatible format-144 lineages (released master artwork, earlier growth prototype), format 145 compact growth, resaving and owner/shared continuation. The save floor remains58. Format146 combines artwork and growth; replay floor146, protocol64 and SIM31 distinguish the integrated engine. This gate change is required by master's independent format 144/SIM30 allocation, not by adoption of any optimization.
- Untouched final master replay traces match the earlier confirmation master on all eight fixed 1,024-tick inputs. Separate final saves and stock/statistic inspections are under `current-master-work/` and `retained-work/`.

Load-only layout metadata was placed in existing padding: sizeof(Map) and the checked hot-field offsets are unchanged (`layout-check/results.json`). No performance conclusion is based on the compatibility tests. Current output saves are not accepted by old executables; old supported saves continue loading through versioned readers.


## Final building-artwork integration

After the previous benchmark, master advanced to `67fd5b935` (building families/artwork and presentation-test snapshot fixes). Delivered commit `b7a478122` integrates that base. See `final-integration-freeze.json` for full commit/tree IDs, compiler, linked-library/executable hashes, flags and fixed fixture hashes. Source was built immediately before committing the identical tree; the frozen executable hash identifies the tested artifact.

- Final broad run: 238 passes, 11 display skips (`final-integration-broad.xml`) ; 29 unit cases pass (`final-integration-unit.xml`); 12 golden cases pass (`final-delivery-golden.xml`).
- The initial broad run caught a stale replay-floor constant (146 rather than147). That was corrected and the full selected broad suite rerun successfully. The failed run remains under `final-integration-broad-first`; it is not omitted from evidence.
- Final per-tick matrix:144 records including 24 reused baseline references, 120 fresh final-executable runs. All eight fixtures match edb09d402 for delays 1/3/8, owner 4 and shared 1/2/4/8 (`final-delivery-verification/results.json`).
- Both final retained and untouched-master 1,024-tick runs reproduce their earlier 6487-based replay traces and resource/statistic totals (`final-work-trace-comparison.json`, `final-work-stock-comparison.json`). Final playable saves and inspections are in `final-retained-work/` and `final-master-work/`.
- Seven real save fixtures now cover released144 map artwork, the growth144 prototype, released145 building artwork (nonempty/empty), growth145 compact proposals, and growth146 integrated map artwork. Each loads/resaves/continues across owner/shared execution. The maintained fixtures and provenance are committed under `test/fixtures/resources/growth-save-layout/`.
- Additional header cases cover binary/text, full/partial headers, empty/nonempty resource declarations, enabled/empty experiment keys, EOF and following-record alignment. The format 145 probe reads bounded fields without mutating GameHeader and restores the stream position before normal validation.
- Current save/replay format 147, protocol 65, SIM 32 combine building artwork with pending growth. Save compatibility floor 58 is unchanged. Format146 remains readable. The150 seeded-resource digests remain unchanged; the146→147 golden checksum difference is solely header XOR 33554432. The match record and sim-version gate check were refreshed against 67fd5b935.

No A–D implementation is present in the final source. The extra version integration is unrelated to candidate adoption. Platform limitations above still apply; no new non-Linux or display coverage is claimed. Timing starts only after all final builds and checks finish.
