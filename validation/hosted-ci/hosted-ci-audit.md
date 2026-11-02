# Hosted CI audit for Maxima staffing caps

Audited 2026-11-02 (UTC), using authenticated GitHub job logs. No source changes, cancellations or restarts were performed by this audit.

Run: https://github.com/Globulation2/glob2/actions/runs/41446477947

PR head: `dd38ad2902d3860669c42992a994bd288e96719b`. Tested PR merge: `9c8fc892581feeafe65e11a10c5ef99e63a25091`, merging that head into `9ece139a26b84caec0045dc688817315b19fe08b`. Selector succeeded and selected all 64 logical checks. This is evidence for the old head/merge, not proof that the later three-field JSON fixture metadata revision or new master base passed hosted CI.

## Native successes

- Windows MinGW job [125284414565](https://github.com/Globulation2/glob2/actions/runs/41446477947/job/125284414565) succeeded. Engine runner: 1,807 passed, zero failed, 270 skipped. Maxima released v115 checkpoint matched **all 512 complete checksum-record hashes**; save/reload at tick 30256 preserved **all 256 complete continuation records**. Default and parallel gradient schedule verification steps succeeded. Both JavaScript profile1 and realistic-profile1 passed complete tick checksums and exact 1/4-worker replay/save output (one and five saved continuations respectively).
- macOS native job [125284415489](https://github.com/Globulation2/glob2/actions/runs/41446477947/job/125284415489) succeeded. Engine runner: 1,808 passed, zero failed, 272 skipped. Selected CLI/continuation and generator fingerprint checks succeeded. Both JavaScript profiles passed complete tick checksums and exact 1/4-worker replay/save output. These logs establish the stated profile checks; they do not establish every possible Maxima replay across every platform.
- Retained artifacts: [Windows deterministic traces](https://github.com/Globulation2/glob2/actions/runs/41446477947/artifacts/13147400585), [Windows released compatibility](https://github.com/Globulation2/glob2/actions/runs/41446477947/artifacts/13147345632), [macOS deterministic traces](https://github.com/Globulation2/glob2/actions/runs/41446477947/artifacts/13148531463).

## Eight failed jobs: actual causes

| Job ID | Actual failure and relevance |
| --- | --- |
| 125284414531 Native Clang coverage | Recording dependency HTTPS download failed with SSL handshake timeout before coverage build completed. Infrastructure/download failure; no failing coverage test result was produced. |
| 125284414805 Platform stack | Container engine-build recording dependency download timed out; stack initialization failed before smoke scenarios. Infrastructure/download failure. |
| 125302825952 Chromium 1/5 | 51 passed; two failures: campaign discard persistence recovery could not find `back` for 30s; colony-skins could not reach main menu after declining introduction for 60s. Both occur in browser UI/persistence/onboarding paths, without an allocator or checksum assertion. |
| 125302826060 Chromium 2/5 | 28 passed, one skipped; Studio serial/threaded expected sim120 but received121; additionally threaded Introduction engine-suite host never set `introDone` within500s. The latter is an unresolved execution timeout, not a demonstrated fixture-version mismatch. |
| 125302826139 Firefox | 42 passed; only two Studio serial/threaded expected120/received121 assertions. |
| 125302826178 Lifecycle | All three Playwright lifecycle tests passed; job failed only two native Studio reference unit assertions (fixture120 vs current121). |
| 125302826255 WebKit | 41 passed; two Studio version assertions, plus campaign-authoring timeout in initial `page.goto` waiting for load (90s), before editor actions. |
| 125302828494 Threaded colony skins | One passed; one failure finding `intro/menu` (30s), before custom match starts or mesh/context restoration assertions. |

The stale Studio fixture metadata is directly relevant to the simulation revision change and requires the reviewed fixture refresh. The other browser timeouts are on paths untouched by the staffing implementation; that source-level separation does **not** prove they were pre-existing or harmless. No baseline reproduction was run. No additional allocator, AI outcome, or complete-checksum mismatch appeared in the inspected browser failure logs. The Introduction timeout remains a real incomplete browser test, not a pass.

Relevant browser evidence: [Chromium1 job](https://github.com/Globulation2/glob2/actions/runs/41446477947/job/125302825952), [Chromium2 failure traces](https://github.com/Globulation2/glob2/actions/runs/41446477947/artifacts/13147302719). Job links remain authoritative if an artifact expires; full raw logs were not duplicated locally.

## Remaining limits

Serial renderer3/3 succeeded (two passed, two skipped). Renderer1/3 and2/3 were cancelled; browser/native simulation-equivalence job was also cancelled. These checks are unavailable, never counted as passes. Old run41446477947 completed with conclusion `cancelled` after the PR update superseded it; all three cancelled checks remain unavailable. The newer full run41459623621 against the updated base had started selection at handoff; this audit does not substitute for its results. Latest master browser-compositor changes can affect browser interaction but are not evaluated here.

Compact failure excerpts and platform verification excerpts are adjacent to this report. This report supports an explicit maintainer assessment with known incomplete hosted browser coverage; it does not describe full CI as green.
