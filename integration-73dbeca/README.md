# Final rebase and installation verification

Tested implementation: `73dbeca5dd7a3ad8641626598e3029b40cc3f958`.
Base: `87a8b25284920c00c224786c0cb1930d21f3d337` (`master`, the repository's main branch).

The final rebase includes the newly merged soundtrack pipeline and master’s subsequent skin, launch, online workspace and installation changes. Music assets, generation recipes, decoder and recording source remain identical to the [complete integration and audio evidence](../integration-03f1395/README.md). All 32 SHA-256s in that inventory still match; its nine generation provenance/QA records, ten sets' comparison clips and per-track sizes apply unchanged. Total remains 29,463,067 -> 18,116,880 bytes, 38.51% smaller. No simulation/save/replay/network version changes are introduced by this PR.

Environment/dependencies/flags are those in the linked complete record: Ubuntu 26.04.1 x86_64, GCC 13.4, repo Python 3.14.4/music Python 3.11.16, FFmpeg 8.0.1, native opusfile 0.12/opus 1.6.1; browser managed Emscripten, pinned opusfile 0.12/opus 1.5.2/Ogg 1.3.6, Node 22.22.1/Playwright 1.63.0. Native release -O3/gnu++20, browser -O2/gnu++20/wasm exceptions, pthread for threaded. Build logs contain exact flags.

## Refreshed commands

```
OPENBLAS_NUM_THREADS=1 OMP_NUM_THREADS=2 tools/music/.venv/bin/python -m unittest discover -s tools/music/tests
python3 -m unittest discover -s test/build_system -p test_music_encoding.py
python3 -m unittest discover -s test/build_system -p test_web_assets.py
node --test browser/unit/*.test.js
GLOB2_SDL3_PREFIX=$PWD/build/sdl3-opus/prefix scons -j10 release=1 server=0 CXX=g++-13 CC=gcc-13 tests build/linux/client/release/src/glob2
GLOB2_SDL3_PREFIX=$PWD/build/sdl3-opus/prefix scons -j8 release=1 optimized_assets=0 server=0 CXX=g++-13 CC=gcc-13 install INSTALLDIR=$PWD/artifacts/opus/latest-install-root BINDIR=$PWD/artifacts/opus/latest-install-bin
python3 artifacts/opus/check-installed.py
GLOB2_TEST_FFMPEG=/usr/bin/ffmpeg python3 test/run_tests.py --filter 'MusicSet/*' --filter 'SoundMixerTrackSelection/*' --filter 'GameplayRecording/*' --filter 'RecordingSession/*' --filter '*PlayerVoice*/*' --filter 'GameMusicController/*' --filter 'GameplayRecording.Integration/*' --junit artifacts/opus/latest-audio-tests.xml --artifacts artifacts/opus/latest-audio-artifacts --verbose
LD_LIBRARY_PATH=$PWD/build/sdl3-opus/prefix/lib build/linux/client/release/src/glob2 --verify-match test/fixtures/multiplayer/FourSquares1.g2mr --map maps/FourSquares1.map.gz --out artifacts/opus/verify-73dbeca
cmp artifacts/opus/verify-73dbeca/checksums.txt test/fixtures/multiplayer/FourSquares1.verify-trace.txt
python3 browser/build.py web-package web-tests
# From browser/:
npx playwright test threading.spec.js recording.spec.js staged-assets.spec.js --grep 'serial audio produces PCM|serial all soundtrack|serial recording segments full|serial interrupted recording recovers' --output=../artifacts/opus/latest-browser-serial-results --reporter=list
npx playwright test threading.spec.js recording.spec.js staged-assets.spec.js --grep 'threaded audio produces PCM|threaded all soundtrack|threaded recording segments full|threaded interrupted recording recovers|later packages arrive' --output=../artifacts/opus/latest-browser-threaded-results --reporter=list
```

Music 121/121, encoder/install 5/5, assets 15 cases (one unrelated skip), browser JavaScript 44/44; native audio/recording 34/34; all 702 golden checksums match. Native game and harnesses rebuild after the rebase. The final SConstruct follow-up only changes installed linker flags; the final install relinks and native cases rerun successfully. Browser serial/threaded runtimes rebuild and are refreshed again at the final commit.

Master's SDL prefix fix filtered `RPATH`, but this SCons parser puts `-Wl,-rpath,<prefix>/lib` in `LINKFLAGS`. The small final follow-up filters both. The retained initial install log shows the problem; `rebase-latest-install-fixed.log` and `rebase-latest-install-check.log` establish the correction. The installed binary has only its relative library RUNPATH, starts with the development SDL prefix renamed unavailable and LD_LIBRARY_PATH unset, and links Opus/Ogg with no Vorbis. Installed assets: exactly 32 Opus music files, nine LICENSE files, no Vorbis music. This fixes the prior local RUNPATH limitation; it does not certify store/release packaging.

Final browser checks pass 12/12 serial and 15/15 threaded/background across Chromium, Firefox and WebKit. All 12 unique MP4 exports contain 48 kHz AAC with zero audio/video start offset; normal durations differ by less than 1 ms, recovery exports by 14 ms of AAC tail rounding. Results, timestamp probes and exports accompany this record. See the full integration record for coverage rationale, native recording/video comparison and decoder trimming/failure tests. Historical Chromium stall-tail and Firefox intermittency evidence is retained there; no universal stall/driver fix is claimed.

Windows/macOS/iOS execution, Android device/audio hardware checks, human listening/play review, the full slow engine suite and store/signing certification remain outside final local coverage. Previous-revision platform builds are historical evidence only. Hosted full CI is requested separately. Maintainer listening/acceptance is pending, with accessible samples attached.
