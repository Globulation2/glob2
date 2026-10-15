# Final entity traversal verification

This supersedes the earlier handoff/checkpoint. Implementation and review fixes are complete. No performance benchmark or Callgrind measurement was run for this change.

- Final PR commit: `b44b7413fa399a6917ae5806a82964d380797452`.
- Integration base: `e7572da6be15566bf20086f40a73a0f4b7c2a83d` (master fetched before final validation; no newer master commits at that check).
- Runtime source reviewed and tested at `3d6ffc413`; the final commit only refreshes four golden fixtures. Client/engine/unit targets were rebuilt at the final SHA, then the committed match, conversion-phase, Maxima budget and deep snapshot corruption cases were rerun without update mode.
- Environment: macOS26.6.2 ARM64, Apple Clang21.0.0, Python3.14.7. Runtime dependencies unchanged. Existing managed dependency prefix reused; exact paths/flags are in the build logs and dependency manifest hashes in environment.txt/environment-reviewed.json.
- Build: `python3 tools/dev_build.py -j4 engine-tests unit-tests build/darwin/client/debug/dev-dev_fast-true-linker-auto/src/glob2`; fast development `-O0 -g1`, ordinary compilation (no PCH/unity), not release qualification. Passed; see review-build.log, review-fixes-build.log and review-final-build.log.
- Review: independent subagent reviewed lifecycle/cursors, legacy/current saves, compact observations, scripting, scope/minimality, tests, comments, docs and architecture. No runtime correctness defect found. Both recommendations addressed: remove redundant per-record ownership auditing from AI view construction (retained in deep verification), and extend the real conversion regression for earlier/later team phases. Final review approved after fixes, conditional only on final committed-match verification.

| Validation | Result | Evidence |
| --- | --- | --- |
| Focused engine: population lifecycle, exact166 and legacy164/165 saves, failed-load recovery, snapshots, scripts, historical stock, real conversion phases, Maxima budget |99 passed,0 failed;2 display cases skipped |integrated-focused.log/xml |
| Focused unit: RNG/checksum/BitArray, replay gates, hiring, Maxima pure logic, identity/layout |91 cases passed,0 failed |integrated-unit.log/xml |
| All quick engine golden cases, including old143/84/88 continuation and multiplayer record |15 passed,0 failed |integrated-golden.log/xml |
| Final SHA committed record + real conversion + Maxima budget + snapshot corruption, no update mode |4 passed,0 failed |review-final-gate.log/xml |
| Sim revision/record coherence, private RNG ownership, whitespace |All passed |review-contracts.json |
| Fresh8-player128x128 mixed Nicowar/Castor/Numbi/Maxima, map seed4242/game seed19, serial vs8 compute threads to256, then loaded continuation to320 |Checksum traces, replay bytes and saves byte-identical between thread counts |determinism-reviewed/{commands,hashes}.json and actual saves/replays/checksums |
| Loaded vs uninterrupted same game through320 |All64 aggregate CRCs and detailed team/entity records match |determinism-reviewed/uninterrupted/{comparison,aggregate-and-save-comparison}.json |
| Focused intrusive heap-cursor ASan+UBSan |Passed with Apple Clang and Linux ARM64 GCC11.4 |sanitizer-final-clang.log, sanitizer-final-gcc.log, intrusive-sanitizer-smoke.cpp |

Exact final test commands and selections are in integrated-test-commands.json and run-integrated-checks.py. The fresh native check scripts are check-determinism-reviewed.py and check-ai-continuation-reviewed.py; output directories and commands are retained. These bounded simulation checks establish correctness evidence, not speed estimates. Generic sanitizer source/header inputs are unchanged by subsequent integration; this is component evidence, not a full-engine sanitizer run. The Linux image digest is recorded in the sanitizer log.

The two skipped display cases are JavaScript custom-library/telemetry dialog rendering and terrain/resource permission UI. Earlier broad attempts accidentally selected unrelated TerrainRegistry stress and the unchanged Maxima.Farming CPU timing gate; those timeout/timing failures are retained but excluded from correctness claims. The full stress and AI checkpoint campaigns were stopped, not passed.

Limitations: full Windows/Linux/Web simulation checksum equivalence is unverified; the Linux sanitizer smoke is not game determinism evidence. Full-engine sanitizers, release packaging and a maintainer playthrough were not run. Append ordering, compact AI probes and four useful task candidates per tick intentionally change scheduling/tie breaking; no old-version trajectory equality is claimed. Uninterrupted/reloaded raw save bytes differ in diagnostic telemetry/hash data, so full byte equality is only claimed between thread counts; continuation equality is stated in terms of the aggregate and detailed simulation records. The direct Maxima executable-budget regression guards the omitted behavioral field discovered during the earlier byte investigation.

Maintainer authorization: the user explicitly approved merging after subagent review and fixes. Evidence and these limitations are recorded in the PR; no second-maintainer approval or full hosted matrix is claimed. Historical handoff notes and pre-integration logs in this bundle are superseded by this final summary. Pre-integration development saves with provisional ordered165 encoding are not upstream165 compatibility fixtures.
