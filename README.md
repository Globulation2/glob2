# Touch tutorial verification evidence

Implementation: `8ef46a8462d0821eae2a056cd77bec2bbb8e8f8a`.
Base: `9a3a5bcb19cac6ead7abb253993be8406a8cf33a` (mobile selection headers and radial controls, PR #1030).
Master fetched before final validation: `c26a0a02aeef4993a7478c3651d6117b49dbc56c`.
The two subsequent master commits change release NASM installation and browser image-test inventory, with no touch/tutorial integration changes; the branch retains the tested controls base.

## Environment and build

Linux 7.0.0-31-generic, x86_64; GCC 15.2.0 (Ubuntu 15.2.0-16ubuntu1).
Existing SDL prefix: SDL 3.4.16, SDL_ttf 3.2.2, SDL_image 3.4.6, SDL_net 3.2.0.
Native release: gnu++20, -Wall -fPIC -O3 -s, HAVE_CONFIG_H, SDL_MAIN_HANDLED.
No runtime dependencies were added. Final native binaries were rebuilt after committing; test XML includes build provenance.

```sh
GLOB2_SDL3_PREFIX=/tmp/glob2-sdl3/prefix scons release=1 tests build/linux/client/release/src/glob2 -j12
python3 tools/tutorial/build_catalog.py --check
python3 -m unittest discover -s test/build_system -p test_touch_tutorial.py
python3 test/run_tests.py --build-dir build/linux/client/release --binary engine --filter 'GameGUITouch/*' --filter 'LegacyScriptCoverage/*' --filter 'ClientChannels/*' --filter 'WorldSnapshot/*' --filter 'JavaScriptPresentation/*' --filter 'SimulationReadPhase/*' --filter 'ReadOnlyPhase/*' --filter '*Save*/*' --filter '*Replay*/*' --filter 'EngineSession/*' --filter 'GUIInteractionCoverage/*' --exclude-tag benchmark --quick -j4 --display-jobs 2 --artifacts artifacts/touch-tutorial/committed --junit artifacts/touch-tutorial/committed.xml
python3 test/run_tests.py --build-dir build/linux/client/release --binary unit --filter 'BidiText/*' --filter 'MobileInput/*' --filter 'UILayout/*' --filter 'BrushToolHit/*' --filter 'PanelButtonHit/*' --quick --artifacts artifacts/touch-tutorial/committed-unit --junit artifacts/touch-tutorial/committed-unit.xml
build/linux/client/release/src/glob2 --verify-match test/fixtures/multiplayer/FourSquares1.g2mr --map maps/FourSquares1.map.gz --out artifacts/touch-tutorial/golden-serial --compute-threads 1
build/linux/client/release/src/glob2 --verify-match test/fixtures/multiplayer/FourSquares1.g2mr --map maps/FourSquares1.map.gz --out artifacts/touch-tutorial/golden-parallel --compute-threads 2
```

## Results and coverage

- Catalog check and Python contract pass: 116 messages, 968 unique source aliases, eight exact bundled source revisions, seven complete localized entries for every page. Reused Spanish source text resolves to its distinct stable message IDs through the published script cursor.
- Focused native engine: 132 pass, one failure, 133 cases. All new tutorial/SGSL cases pass. Coverage includes local Back/Continue, one final acknowledgment, collapse/scroll/history without acknowledgment, recap deduplication, rotation, unmatched scripts, save/resume at acknowledgment and scenario-wait boundaries, snapshot/read-only boundaries, script channels, retained saves and replay cases.
- Focused unit: all 50 cases pass, including matching every Arabic/Persian catalog value against fribidi.
- Nine tutorial traces: chapters 1, 2, 4; desktop, Compact, Spacious with queued script-client presentation and two map compute workers. Each chapter uses an identical saved setup, seed 123, empty order stream, acknowledgment ticks divisible by five, 128 ticks. Three traces for each chapter are byte-identical, and rendering does not alter completed tick checksums. Initial saves and trace files are attached.
- Golden match: serial and two-worker compute both verify 842 checksums, byte-identical to the committed FourSquares1 golden. Replay, result, compute telemetry and checksum files are attached.
- 21 native UI screenshots: three loadable chapters, portrait 390x844, landscape 844x390, Spacious 1200x900, both thumb sides; enlarged Arabic at 568x320 with safe insets 24/20/24/20 and the row fallback. Responsive dimensions can round one pixel. Screenshot names encode chapter, width, thumb side. These are harness captures, not physical-device gameplay.

## Known failures and unavailable coverage

Keep the implementation PR draft.

Chapter three's shipped format-81 map is rejected by the existing SGSL area loader (old objective bytes become an invalid area). Source fingerprints/aliases and translations are checked, but chapter-three GUI gameplay, screenshots, complete save/resume and checksum runs are omitted explicitly. No SGSL loading, maps, source scripts, simulation rules, or version gates are changed by this PR; repairing the old loader needs separate compatibility work.

The existing flag-drag harness asserts that dragging must not pan the camera. It fails in the broad final suite. Replacing GameGUITouch.cpp and GameGUITouchView.cpp with their original master-base versions and rebuilding reproduces the same assertion; that diagnostic uses the current harness/remaining objects, not a complete clean master build. Logs are attached. An attempted SDL_SyncWindow fixture change did not fix it and was removed from the final revision.

Earlier broad unit verification: 930 pass, one failure across 931 cases. SDL_image decoding of the 16-bit RGBA normalization fixture fails; the image test source is byte-identical to master. This is not a full baseline build comparison. Earlier Python build-system verification: 410 cases, four failures/four errors, three skipped; all eight failures concern existing package-assets tests. A master-source archive reproduces those eight (plus a missing source PNG in that reduced archive). Diagnostic logs are attached; these broader exploratory runs preceded the final committed rebuild.

No Android or iOS devices were available. Chapter-one phone playtesting, all-four-course completion/chapter unlocking on both platforms, physical-device gestures, cross-platform checksums, full separate baseline/adapted builds and a threaded simulation-owner tutorial run remain unverified. Catalog translations are complete but require native-language/gameplay review before release. Manual phone validation is still the planned gate before accepting chapters two through four for release.

## Artifact integrity

SHA256SUMS lists every evidence file except itself. Main source contains only durable documentation; this separate evidence branch holds logs, screenshots, saves and replays.
