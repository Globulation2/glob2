# Independent follow-up review of PR #531

Reviewed commit `9c0ac5b1c44c8c532bc02b9e2a8d45f597d0c05f` and current source read-only. No builds, runtime suites, commits, pushes, or posts. Parent-reported successful test runs are not independently rerun evidence.

## Original recommendations

- **Golden platform coverage:** addressed in code. `MapGeneratorGoldenCoverage.h` checks only the requested platform and refuses mixed/stale local revisions. Its unit test includes current foreign/stale local, absent local, mixed local, and restored-current cases. Strict checking requires local coverage for each registered generator. Actual updated Linux/Windows rows remain pending, so those platforms are not verified here.
- **Image capacity:** addressed. Import uses the live cap; the image test creates sixteen distinct, well-spaced markers, imports/exports/reimports, checks colony count and start positions, and rejects a seventeenth marker with the expected diagnostic. CLI documentation reflects the expanded cap.
- **Legacy Maxima continuation:** substantially addressed. The retained-v126 test now compares serialized AI state, restores separate simulation RNG states for paired branches, compares returned order payloads, compares heavy simulation/entity vectors across 128 ticks, and compares final AI serialization. The sixteen-team case installs actual Maxima controllers in slots zero and fifteen before saving and continuing. New count/truncation probes exist, with one encoding defect detailed below.
- **Warrush rationale/docs:** most corrected; AI tuning, Version.h and development reference now explain null-slot fallthrough. CLI and Savannah active contracts were updated. ReplayReader.h still contains the old rationale below.
- **Alliance storage:** addressed. Arrays derive from Team::MAX_COUNT, and the test exercises alliance/enemy/chat masks for player fifteen.

## Remaining actionable issues

1. **P2 verification fidelity — collect AI orders before applying them.** `test/support/EngineFixtures.cpp:23–29` calls getOrder and immediately executes each player's order. Later AIs can observe earlier orders from this same tick. Production `src/EngineRun.cpp:120–172` collects decisions before `executeOrdersAndStep` applies them; the parallel path explicitly says decisions see the same completed tick. Store `(player, order)` pairs and wire payloads while polling every AI, then apply the collected orders in player order, then syncStep. This was inherited from the previous individual harness, but the newly shared scheduler is the appropriate single place to align continuation evidence with production. Re-run the targeted TeamLimit and existing AI telemetry continuation tests after changing it. A continuation passing under an alternate scheduler does not establish equivalent production decision visibility.

2. **P2 test coverage — encode malformed slot counts in network byte order.** The new `test/TeamLimitTest.cpp` probe patches bytes with `slots >> (8 * byte)`. `libgag/src/BinaryStream.cpp:27–35` writes Uint32 using htonl. Intended count one therefore loads as 0x01000000 and tests an oversized count, rather than `savedTeamSlots < mapHeader.getNumberOfTeams()`. Use shifts `8 * (3 - byte)` or patch with bytes produced by BinaryOutputStream. Zero/17/UINT_MAX still reject, but the undersized-capacity branch is not presently exercised.

3. **P3 comment correctness — finish the replay rationale correction.** `src/ReplayReader.h:25–26` still says changed Warrush opening schedules can make old replay orders produce different simulation. Extra null-slot probes do not change smaller matches, and replay order execution is distinct from live AI polling. Explain the new capacity, counted saved state, and JavaScript generation checksum-layout boundary accurately, without asserting an established old-Warrush simulation divergence.

## Coverage limits

Source review confirms the new checks' intended mechanics. The parent reports a clean native build, full unit suite, image suite, core suite and macOS strict goldens passing. The old/current twelve-Warrush paired run was still underway at task dispatch. Linux/Windows CI were queued and updated foreign golden rows were unavailable. No cross-platform per-tick equivalence or human playability result is inferred from native tests or checked-in macOS rows.

## Final precise-fix verification

Re-read the pending worktree diff after the three follow-ups were revised. All three are addressed:

- The shared scheduler now polls and serializes every AI decision before executing any pending order, preserving player/sender order and advancing one simulation tick afterward.
- Count rejection probes now patch bytes produced by BinaryOutputStream.writeUint32. Intended count one therefore reaches the undersized-capacity check.
- ReplayReader.h now explains the capacity and state/checksum-layout boundary without claiming smaller-match Warrush divergence.

No outstanding actionable source issue was found in these precise revisions. No runtime tests or builds were rerun by this reviewer. The parent reports the old-v126/current twelve-Warrush 128-tick sidecars match byte-for-byte; affected native reruns were underway at dispatch. Actual Linux/Windows validation and foreign golden regeneration remain pending and are not certified by this source review.
