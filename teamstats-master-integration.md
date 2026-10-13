# PR #1152 master integration and merge qualification

Final tested head: `ef23867049e1f3a6ae89e6188fa2a3e05d765d2b`. Rebase/merge base: `6b95c81c0d438f64e3391ff4785bc95d3436d174`. Platform: Darwin arm64, Apple clang version 21.0.0 (clang-2100.3.34.2), Python 3.14.7. Build: `tools/dev_build.py`, `dev_fast=1`, `-O0 -g1`; correctness only. Final binaries report clean source provenance. Exact commands, binary hashes, compiler flags, raw logs and JUnit reports are in the archive.

## Integration

Replayed the final feature diff on master after retaining pre-rebase history locally. Master’s standard simulation/presentation capture policy is preserved. Statistics inputs join the standard simulation view; statistics never contribute capture masks. `statisticsView()` leases map components only at diagnostic ticks. Presentation leases omit event inputs. The simulation owner calls `prepareReadBoundary()` for fixed-deadline publication and admission; pure `captureReadBoundary()` schedules no work for UI, saves or tests. Engine, headless stepping, paused/waiting boundaries and immediate publication use these services consistently.

An independent sub-agent reviewed the integration and historical-header repairs and found no blocker. Snapshot-buffer reuse, contiguous team ranges, executor wakeup changes, frame reuse, opt-in clocks and fixed deadlines remain. No MCP diff remains; master already contains those repairs. Save format 158/floor 58, replay floor 158, network 70 and SIM_REVISION 53 remain. Pending serialization and older-save migration are unchanged.

## Verification and repairs

- Final fast unit selection: 100 passed, zero failures/skips. Includes 47 executor cases, read phases, 22 gradient pipeline cases, replay acceptance, experimental headers and 11 header round-trip/compatibility cases. Executor cases take 0.7 seconds; whole selection 7.9 seconds of reported test time.
- Broad integrated engine selection at `8f3379f3e72d2627bc3ea5f3ffd5d6ac4aec4f2c`: 263 cases, 246 passed, eight failures, nine display skips. Failures were obsolete replay acceptance (one), action tests reading delayed statistics (five), a high-ID test expecting an aggregate before its deadline (one), and a stale fixed checksum fixture (one). The full failure logs are retained.
- Repairs use live collector fields only for immediate cumulative-event assertions; instantaneous/published measurements still use the published accessor. High-ID aggregates are checked as pending across save/load, then visible after their deadline. The retained 152 replay now verifies rejection at the 158 replay floor; old save readability remains separately covered. Synthetic historical headers remove the new delay byte and catalog fields; malicious building-delay tests target their new byte offset. Stats delay gets binary/text, full/playerless, invalid-value, old 154-zero-default and sentinel-alignment coverage.
- Regenerated the 96-tick capability-ablation checksum fixture because deterministic checksums now include the statistics queue/publication state. Its two-run per-tick/RNG comparison is retained and passes. Main multiplayer golden record/trace passes unchanged.
- Final repaired selection at final head: 133 passed, zero failures, seven display skips (140 cases). Reruns every affected unit-customization and engine-session case, all statistics pipeline/snapshot cases, and the main golden match.
- Combined current engine inventory: 254 passed, nine display skips, no unresolved failures. `case-inventory.json` maps each case to its verified revision and JUnit file. After the broad run only test oracles/fixtures and a replay-floor comment changed; simulation execution code did not change.
- Included AI worker-count equivalence and save continuation, custom catalogs, resource-growth formats 144/145 continuity, history/smoothing boundaries, restored pending work, two engines with AI under latency/jitter/loss agreeing per tick, relay/verifier agreement, replay/network gates, and publication/source generation checks. Generated saves, match records, replays, checksums and verdicts accompany the logs.
- Simulation-version guard and `git diff --check` pass. Master was fetched again before merge; it remains the recorded base.

## Limits

No fresh release profiling, shipping/package qualification, sanitizer rerun, actual Linux/Windows/browser/mobile engine execution, cross-platform checksum comparison, or maintainer GUI/gameplay review for this rebase. No speedup claim is made. Earlier release profiles remain evidence for their recorded revisions. GPU-specific convergence limitations documented in earlier comments are not requalified here. Hosted cheap contracts do not substitute for this local verification or omitted platform execution.
