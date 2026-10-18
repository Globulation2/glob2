PR #1194 final review and verification

Tested head: c6bd50a7ae64ad80f6a747e888aae822f829b653
Integrated and compared master: c7cdf04d4fbcdd4d09f81e2953d6a5e77e5ee4b2

Independent review removed redundant synchronous building/air adapters and unused readiness APIs, restored the authoritative zero-natural-source fast path and periodic gating, corrected stale fixture/browser expectations, clarified ownership/coalescing/legacy save decoding, and updated the existing simulation/testing guides. No new runtime dependencies. The independent reviewer found no remaining concrete blocker.

Ground material/market/consumer and guard/clear/forbidden fields use complete generations and deterministic FIFO admission. Cold demand returns Pending; warm refreshes retain the old field. Requests coalesce into the existing ticket, edits after capture await a subsequent refresh, and queue waiting precedes the configured delay. Live step safety remains enforced. Authoritative absence returns an immediate shared negative field. Building cold initialization, lazy frontier,64-request overflow fallback and air routing retain their existing implementations. First-use/paint response latency changes gameplay; no maintainer playthrough is claimed.

Compatibility: save/replay format 169; historical save floor 58; SIM_REVISION 71; network protocol 72. Existing supported saves still load. Affected current golden fixtures and committed match record were refreshed.

Performance methodology

Apple M3,8 physical/logical cores,24 GiB,macOS 26.6.2 ARM64,Apple Clang 21,Python 3.14.7. Both final master/PR binaries rebuilt with ordinary matching scons --config=force release=1 -j2 (no dev_fast/PCH/unity), CPU gradient backend, explicit1/8 participants. Normal desktop activity remained. Config hashes after source-path normalization match. Exact binaries, flags and provenance are in final-build-provenance.json and build logs.

compare.py uses the original hashed input manifest: six workloads/five maps,8192 ticks for fresh games and4096-tick checkpoint windows. Each of3 revisions x2 participant counts x6 workloads has1 discarded warmup and10 measured repetitions,396 runs / 360 samples. Variant order rotates/reverses. Simulation TPS uses actual window ticks/run_ns;128 warmup ticks in loaded windows are excluded from CPU/p99 counters. Checksum export is disabled during timing. Compiler/linker/test activity is checked before/after each measured run; after detected activity the driver waits45 quiet seconds and discards/retries contaminated samples. The earlier interrupted campaign is EXCLUDED in its entirety and retained only as metadata/raw rows. No contaminated sample was retained in the final campaign.

The third reference is the earlier PRc48b34539 based on64c792a61; it predates current-master integration. It is an historical reference, not an isolated measurement of cleanup. AI trajectories differ between revisions, so no fixed-work attribution or universal non-regression is claimed. Some earlier-PR reference medians are faster than the final PR; all final master-vs-PR median TPS comparisons improve. The smallest gains are near ordinary timing noise. Full before-reference data and population counts remain in CSV/raw rows.

Final master-vs-PR median TPS (1 participant /8 participants)

SmallForTwo / 2 players           +1.8% /   +1.4%  (8p 39570 -> 40128TPS)
FourSquares1 / 4 mixed AIs         +0.8% /   +2.9%  (8p 22668 -> 23327TPS)
Archipelago / 5 mixed AIs         +13.5% /  +18.7%  (8p 15557 -> 18467TPS)
Oazis 256 / 11 mixed AIs            +9.1% /  +13.9%  (8p 4973 -> 5666TPS)
Playground 12k checkpoint       +14.7% /  +33.3%  (8p 7784 -> 10379TPS)
Playground 24k checkpoint       +17.9% /  +38.9%  (8p 7190 -> 9984TPS)

Geometric mean,1 participant: +9.4%TPS.8 participants: +17.4%TPS, -14.8%simulation time, -7.3%measured CPU, -45.9%p99 tick time, -0.8%peak RSS.

Validation

Final head native build: tools/dev_build.py --config=force engine-tests unit-tests -j2, dev_fast=1 -O0 -g1, no PCH/unity.80 focused engine cases pass, covering cold/recurring publication, FIFO/cache/ownership, live edits, binary/text and historical save boundaries, hiring, checksum adapter and committed match verification. Full unit inventory1192: 1101 pass, 2 fail, 89 skip. Functional resource stress1 case passes (six custom-catalog/size variants); its development timings are ignored. Sim-contract Python 9 tests and final sim-version contract pass. Browser JavaScript syntax and diff whitespace checks pass.

Baseline latest master native build and full unit inventory1190: 1099 pass, the same2 failures,89 skip. ImageAssets 16-bit rounding and OpenCL full-field convergence reproduce in selected rechecks. LAN idle-host latency also reproduces on both latest master and PR: master 79 ms vs 50 ms threshold (PR 135 ms in earlier loaded run; magnitudes are not isolated). These are existing master failures, not claimed resolved here.

Earlier broad native coverage before final API cleanup/current-master rebase:1612 selected cases,1610 eventual passes after correcting fixtures, the existing LAN failure, andone MapGeneratorRegistry/catalog generation contracts timeout after1200 s under compilation contention. Logs retain original failures and subsequent passing reruns; coverage-summary.json identifies them. Initial broad execution was interrupted by ENOSPC and resumed using the recorded uncompleted inventory. An economic-script fixture update initially exceeded120 s and subsequently passed update/normal reruns with600 s timeout. An initial stress selection matched0 cases and is NOT counted; the explicit --tag benchmark functional rerun is counted.

24 final release correctness runs: all3 per-tick streams (replay components,snapshot,fullworld) match between1/8 participants for every workload within EACH final revision. Prefixes are1024 ticks, including checkpoints. Full timing windows also assert identical initial/final checksums,outcomes and tick counts across participants/repeats within a revision. Large component detail is removed after digest comparison; its hashes plus compressed snapshot/fullworld traces are retained.

Limits: no new Linux/Windows/browser/mobile runtime verification, renderedFPS, release packaging or maintainer playthrough. Browser test inventory was repaired and syntax checked, but browser execution remains unverified locally. The long generator-catalog timeout is a coverage omission; no pass is claimed. Hosted CI status is separate and may be pending. Baseline failure logs and all applicable coverage/commands are retained.

Portable evidence includes raw final measurements/results/logs, both final revision trace sets, input maps/checkpoints and hashes, JUnit, commands, source/binary/build provenance, review notes, representative queued-publication binary/text saves, and custom-resource stress exports. Bulk AI-continuation save data and old binaries/object caches are omitted; associated JUnit/logs remain. Disk/cache cleanup manifests explain deleted rebuildable outputs. Nothing from this evidence folder is committed to the product branch.

Reproduction: extract evidence.tar.gz under a matching source checkout. Raw commands preserve original checkout paths; substitute your checkout/binary paths and the extracted inputs directory when rerunning. Manifest input hashes, per-run command arrays, build flags and revision/binary hashes remain the exact measurement provenance.
