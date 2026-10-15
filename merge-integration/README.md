# Pre-merge integration refresh

Final PR head: `7904b5d10afcf825cefa42c2789fe105deb9e618`.
Merged master: `d69aca475` (includes `80297dba2`, stream field names via string_view, and `d69aca475`, bounded worker wakeups).

The parent evidence records 96 focused native cases against `6808e584c`, before these master changes. Those full engine/unit binaries were not rebuilt or rerun after this merge. The following targeted integration checks refresh the affected serializer and scheduler boundaries; they do not relabel the earlier native run as final-head execution.

- `WordChecksum`, `EntityRandom` and the complete `ComputeExecutorHarness` compiled and executed together at `-O2` against the merged headers: **59 cases / 141,586 assertions passed**, no skips. This includes updated worker wakeup/parallelism, queued scheduling and diagnostic-mode contracts. The exact compile command and diagnostics are in `merge-integration-build.log`.
- Seven affected translation units passed syntax checks using the included syntax driver: TeamStat.cpp, TeamStatsPipelineTest.cpp, GameAI.cpp, Game_io.cpp, MapMisc.cpp, UnitSerialization.cpp and building/Misc.cpp. The refreshed master supplied the string_view test-double overrides cleanly.
- `StreamWordIntegration.cpp` compiled and ran against the new stream interface: base-class virtual dispatch with a borrowed field-name view preserved a fixed canonical-byte checksum and byte count, while the word mixer retained its fixed reference value. Compile with `clang++ -std=c++20 -O2 -Ilibgag/include` plus the flags from `pkg-config --cflags sdl3-net`; run the resulting executable.
- Simulation-version/golden-key validation, RNG ownership and whitespace checks passed after the merge. The committed golden fixture and sim key are unchanged by these behavior-preserving master changes.

The native-platform, full-suite and whole-engine performance omissions in the parent README still apply. These are focused integration checks; no fresh complete engine replay/continuation run on the combined tree is claimed.
