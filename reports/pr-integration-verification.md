# Draft PR integration verification

Tested committed source: `857761d70d6c5783d6ddda3ab3d769756e1ca916`, tree `95a69aed4b88af3ed7f390deab91c190836e1662`, merged with master `8fd9edb0b413a9dc19618e75149458bf525506af`. The committed source matches the compiled/tested working content; the golden record was regenerated before verification. Concurrent tournament-tool/AGENTS edits were excluded.

Final behavior: adaptive prestige is enabled by default for normal prestige victory, with a1–4 allowance (`max_pipelines=4`). Saves use193, network75, SIM120; older saved canonical choices and missing-cap1 migration remain supported. Runtime dependencies were not added.

Linux x86_64 GCC13.3: routine full-feature development build (`python3 tools/dev_build.py engine-tests unit-tests maxima-strategy-dump <client-target> -j8`, `-O0 -g1`) and optimized build (`scons release=1 -j8`, `-O3 -s`) both passed using the existing SDL3/dependency prefixes. Raw bounded compressed build logs and source/binary hashes are included.

- 516 native cases pass, zero failures/errors/skips. Coverage includes all Maxima suites, new wood integration, save/legacy191/current193 continuation, snapshot, victory rules, terrain and turn/replay boundaries.
- 77 Python Maxima tests pass.310 Markdown documents checked with zero errors.
- The integrated active-Prepare full game saves at20,000ticks and resumes identically to uninterrupted20,128. All128 world/replay/snapshot checksum records match development and optimized builds (endpoint83504073).
- Optimized golden match verifies with no rejected orders; valid-CRC network74 record is unverifiable against75; sim-version contract passes against current master. Replay floor/current/future version cases are included in native results.

This refresh is required because master changed Maxima wood selection, not merely because unrelated commits advanced. Only the golden record and SIM revision required manual conflict resolution; both farming and prestige code/test changes were preserved.

The frozen288-game assessment is SIM117 before this integration. It remains historical evidence, not a claim of SIM120 effectiveness. All960 raw probability histories are retained. The archived linear observed-time score predates concurrent trajectory-score tooling; current-metric rescoring is not asserted here. Only3/96 adaptive setups selected capacity above1; none selected4. The user explicitly requested default activation after reviewing that evidence.

Other supported platforms and manual play were unavailable. This remains a draft PR with those coverage limits explicit.

[Full results/hashes](pr-integration-verification.json) · [Committed source snapshot](pr-source.json)
