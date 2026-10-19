# Adaptive16 research candidate: cumulative generation four

Native/test source: e1cac289914889cb7f49a6b9fa1268aae55eaf7d. PR checkpoint98ef41c0f01897e08b5c28049775c1b0a61aacd4 adds only regenerated fixtures. Integration base7c83e71d0303da637d84b0a31eeb1d1adb38f388. Model495713960e3e614040e874e181995e6f5970ae7a99488d47b6465252f00db2d1; generated header89a3936a8e4cc560a1b3b299aa9c804824430bcbab1ce90314d6e2faed429b9a.

The cumulative768-planned-game fit is deployed at benefit threshold16000 milli direction-cost units, H128 offsets8..127, minimum8 and publication delay8, without a maximum age. This is a user-selected research checkpoint. It does not establish stable promotion or universal performance superiority. No spatial inference inputs were added. Historical128000 model metrics and cohort metadata remain intact.

## Focused validation

Mac: Darwin arm64, Apple Clang21.0.0; ordinary release, dev_fast=0, pch=0, unity=0, default linker. Exact build/test commands are in finish-macos.py and compressed build/test logs. The source was frozen for each native build. Tests: the corrected supplier-ledger lifecycle regression;17 golden cases regenerated then independently verified;21 AdaptiveGradientState/ReplayStepCounter cases. The simulation-version checker passed against origin/master. Save version179/replay acceptance floor179; durable save floor58; SIM_REVISION89.

Runtime: fresh1024 ticks at1 thread/probeOFF,1/probeON,4/probeON; old178 mature save16384→17408 with the same three modes; new179 checkpoint16896→17408 continuation. Compare world/snapshot/replay checksum streams and final saves; all four probe CSVs are also equal between thread counts. Continuation validates exact world/snapshot suffixes and final save. Linux uses GCC13.3/glibc2.39 ordinary release on x86_64. Its exact source equivalence, numerical parity, runtime and final comparison receipts accompany this report. All28 cross-platform checksum/save pairs and16 diagnostic CSV pairs matched; the two focused Linux market cases passed.

The supplier-ledger fix clears derived old-world caches before team teardown and standalone map reuse; live pipeline resets preserve the ledger and subsequent turnover tracking. The first new test fixture incorrectly expected raw setMaterial to notify membership changes. The failed log is retained. The corrected fixture uses normal withdrawal/delivery APIs and passes. Production stock semantics were not changed to satisfy the test.

## Raw artifacts

runtime-files.json maps223 original relative paths to exact SHA256 objects in runtime-objects.tar.gz. Extract that archive and retrieve each object from sha256/<hash>; no raw file bytes were changed. This deduplicates identical saves, replays, checksums and CSVs across platforms/threads. Each run includes command/result, checksums, saves, replay and diagnostic outputs; profile preferences are excluded. Original warm-save hash and continuity proof are inside the runtime receipts. SHA256.json covers the published files.

These are correctness/compatibility checks, not performance measurements. Windows, browsers, mobile and the full unrelated native suite were not rerun for this checkpoint. CPU/quality comparisons and research deployment are reported separately. Markets remain unmeasured in AI training; the market tests protect existing engine save behavior.
