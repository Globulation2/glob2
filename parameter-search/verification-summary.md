# Parameter search verification

Implemented declarative search domains for all 68 catalog generators (972 control entries including shared controls). Legal domains, manual normalization, and serialized explicit values remain unchanged. Randomization preserves dimensions, colonies and workers. Appended Random defaults preserve old enum indices and use seed-derived choices. Canals includes both Squares and Hexagons; Fingerprint includes all three patterns and both barriers, following the user's instruction to avoid fitting domains to individual AI flaws.

## Focused verification

- macOS ARM64 and Linux x86-64 optimized client, complete native test binaries, golden fixture, and profiling fixture build.
- All 14 map-generator test cases pass on both platforms at the tuned revision. Final restored categorical domains pass all four new framework/Random/historical-fingerprint cases on both platforms.
- Both macOS UI entry-point/reset/preferences/snapshot/reload/replay cases pass on final code. Every built-in manual control's extremes round-trip through preferences.
- CLI: 50 map commands and 21 diagnostics commands pass; six structured study-tool tests pass.
- Replay-step counter suite and retained/reteamed save case pass; 15 JavaScript save/replay/MatchSetup acceptance cases pass.
- Historical explicit defaults reproduce their pre-Random fingerprints on macOS/Linux. Updated golden rows cover the five changed generator revisions. Full catalog checks compared 564 macOS and 544 Linux rows without unrelated changes before final restoration; the restored rows reproduce that tested configuration.
- SIM_REVISION 11 and regenerated match record/trace. Final macOS/Linux verify-match runs match the committed 701-tick checksum trace exactly.

## Diagnostic studies and limits

- combined: 79,722 recorded requests over 43 generators; 30 generators reached 2,000 requests. Cohorts use separate frozen binaries and seeds.
- combined-final: 10,158 recorded requests over 8 generators; 1 generators reached 2,000 requests. Cohorts use separate frozen binaries and seeds.
- 67 completed four-AI games, each capped at 20,000 ticks, using Nicowar, Cortex, Cabino and Maxima. Default/explicit designs are paired on two seeds; risky numeric boundaries are paired with defaults. One combined high Switchbacks request is explicitly refused before play.
- Replayed exact structural failures: four sampled Switchbacks, Watershed and Tidal Flats failures each reproduce and succeed with adjusted controls. Three of four City States and Fjord samples improve; some shared layouts still refuse. Amphitheatre shared-layout refusals remain and were not redesigned.
- Preview contact sheets cover Maze, Honeycomb Isle and all explicit Fingerprint patterns/barriers. Per-colony resource, fertility, building-space and access reports are retained in the structured map evidence.
- AI openings vary. The observed Canals Hexagons and Fingerprint Islands Cabino stalls are retained as diagnostics, without removing these categories from search/default variety.
- The exhaustive 2,000-roll-per-playable-generator and every-control study matrices are incomplete. Focused verification was used following the user's request to avoid overthinking/overfitting; stopped study processes retain every complete returned request. No universal playability or human-fun guarantee is claimed.
- Windows, Android and browser checks were not run. Existing explicit floating-point geometry has platform-specific fingerprints (notably Fingerprint); the identical-loaded-state simulation checksum boundary was verified across macOS/Linux.

## Evidence

- `study-evidence.tar.gz`: raw request/outcome JSONL, periodic per-colony reports, exact failure replays, catalog, binary hashes and Linux toolchain identity.
- `map-tests-tuned.xml`, `final-focused.xml`, `ui-final.xml`, `acceptance-tests.xml`, `compatibility-tests.xml`: native runner evidence. Logs retain exact commands in the study scripts and build/test invocation logs.
- `ai/`, `ai-boundaries/`: command JSON, generated-map reports, initial/final saves, game results and full economy telemetry. `ai-economy.txt` summarizes the initial completed cohort.
- `*-previews.png`: inspected map previews. `match-final-mac/` and copied Linux traces retain final deterministic verification.

Source base: a676d9274f2ad22b4bdc75c318eca7e7f5b3aa6b. Implementation committed as fdbfbe1519cfcf58d52549b84b64399b57a353af; runner logs include their exact source-content identities. Documentation-only final notes do not alter tested behavior.

## Final integration and tournament follow-up

- Integrated master ddb1fad60 before final native builds. Latest master 7347d3114 adds independent torus rendering; no generator or tournament overlap or merge conflict.
- Python tournaments: opt-in randomize_parameters samples search domains by default, supports legal, preserves shared/fixed settings and explicit overrides, and reuses paired maps. 23 tournament tests, 11 pipeline tests, six study-tool tests and the fairness harness pass.
- Quick-game Caravanserai revision updated to 3. All 30 configured pool revisions match the rebuilt native catalog. 35 queue/warm-map/matchmaker tests pass with disposable PostgreSQL 16; platform typecheck and affected TypeScript lint/format pass.
- Final macOS run: all 14 map-generator, two UI, eight MatchSetup and seven freshly rebuilt ReplayStepCounter cases pass. The golden-record case initially ran before the fixture refresh completed; the retained failure log shows a version mismatch, and the corrected fixture passes its focused rerun.
- Final Linux run: all 14 map-generator, eight MatchSetup, seven freshly rebuilt ReplayStepCounter and golden-record cases pass.
- macOS compares 564 map golden rows with zero failures. CLI passes 50 map and 21 diagnostics commands.
- The regenerated record uses the final sim version. macOS and Linux verify-match produce identical 702-checksum traces, byte-equal to the committed golden trace; check_sim_revision passes against current master.
- Prior studies and AI games were not rerun after unrelated master UI/statistics integration. Full Linux golden revalidation, Windows, Android, browser and exhaustive studies remain unavailable/incomplete; prior explicit-design fingerprints and current cross-platform simulation trace checks cover the changed generation and loaded-state boundaries.
