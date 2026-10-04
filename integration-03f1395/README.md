# Final Opus integration verification for PR #725

Tested implementation: `03f1395b99be79de2e23be711d730b35a5453162`.
Rebased base: `11124f736befac9880dfb7b3448df79129ed9206` (`master`, this repository's main branch). This includes the nine-set music pipeline (#732), release metadata and the new skin shader. The PR is conflict-free; no simulation/save/replay/network version changes are introduced.

Environment: Ubuntu 26.04.1 x86_64, GCC/G++ 13.4.0, Python 3.14.4 for repository tests, Python 3.11.16 for the music virtual environment, FFmpeg 8.0.1, native opusfile 0.12/libopus 1.6.1, pinned SDL 3.4.16 family. The music extras and verified input/render caches were copied into ignored directories from the merged music author's local checkout; recipes and source pins are those in this PR. GPU source separation used the existing RTX 2070 SUPER environment. Browser: managed Emscripten SDK, pinned opusfile 0.12/opus 1.5.2/Ogg 1.3.6, Node 22.22.1 and Playwright 1.63.0. Native release uses `-O3 -std=gnu++20`; browser uses `-O2 -std=gnu++20 -fwasm-exceptions`, with `-pthread` for the threaded variant. Exact compiler/link commands are in the build logs.

## Assets and audible changes

Every new set is regenerated from its recipe's PCM, never from its shipped Vorbis files. All 32 shipped tracks fully decode through the game's libopusfile API; each of the ten trios has exactly equal decoded lengths and supports aligned/end seeks. No `.ogg` remains under runtime `data/zik`. Total: **29,463,067 -> 18,116,880 bytes (38.51% reduction)**. See [every track's sizes and frames](track-sizes.md), [full validation and SHA-256s](all-assets-validation.json), and [source/encoder provenance and QA](generation/).

All nine generated sets pass existing QA (warnings retained). Encoding is fixed at stereo-total 48 kbps VBR, audio application, compression level 10, 48 kHz. Circular PCM history is cropped using Opus pre-skip and end granules; visible frame counts do not change. Large codec peak overshoots reuse the existing circular limiter rather than lowering a whole mood by about 2 dB. No extra normalization is added. Corrections are recorded in `build.json`; small trims remain, including Woodland building. Curious Critters calm uses an explicit 2 ms taper at each loop end to remove the codec seam click. These are audible review points, not a claim of transparency.

[Before/after clips, all mood transitions and all loop boundaries](audio/) cover all ten sets. The PR contains the complete final Opus files. Human listening and play review have **not** been performed by this agent and remain part of maintainer review. Attribution accompanies the third-party clips.

## Commands and results

```
OPENBLAS_NUM_THREADS=1 OMP_NUM_THREADS=2 tools/music/.venv/bin/python artifacts/opus/rebuild-new.py
OPENBLAS_NUM_THREADS=1 OMP_NUM_THREADS=2 tools/music/.venv/bin/python artifacts/opus/finalize-new.py
OPENBLAS_NUM_THREADS=1 OMP_NUM_THREADS=2 tools/music/.venv/bin/python artifacts/opus/finalize-new.py curious-critters
OPENBLAS_NUM_THREADS=1 OMP_NUM_THREADS=2 tools/music/.venv/bin/python artifacts/opus/finalize-new.py orchestral-dawn
OPENBLAS_NUM_THREADS=1 OMP_NUM_THREADS=2 tools/music/.venv/bin/python -m unittest discover -s tools/music/tests
python3 -m unittest discover -s test/build_system -p test_music_encoding.py
python3 -m unittest discover -s test/build_system -p test_web_assets.py
python3 -m unittest discover -s test/build_system -p test_ci_policy.py
python3 -m unittest discover -s test/build_system -p test_mobile.py
python3 artifacts/opus/validate-all.py
```

121 music tests, 5 encoder/install tests, 15 asset tests (one unrelated skip), 18 CI policy tests and 25 mobile build-system tests pass. Generation and installation logs retain the initial failures and the subsequent corrections, including the fractional Surge render bug, codec seam treatment and peak correction. `finalize-new.py` is temporary evidence that re-encodes the generated lossless mastered PCM, validates before installing, and creates comparison clips. It is not an alternative production pipeline. Production is `python -m glob2music build/install` with the committed helpers.

```
GLOB2_SDL3_PREFIX=$PWD/build/sdl3-opus/prefix scons -j10 release=1 server=0 CXX=g++-13 CC=gcc-13 tests build/linux/client/release/src/glob2
GLOB2_TEST_FFMPEG=/usr/bin/ffmpeg python3 test/run_tests.py --filter 'MusicSet/*' --filter 'SoundMixerTrackSelection/*' --filter 'GameplayRecording/*' --filter 'RecordingSession/*' --filter '*PlayerVoice*/*' --filter 'GameMusicController/*' --filter 'GameplayRecording.Integration/*' --junit artifacts/opus/final-audio-tests.xml --artifacts artifacts/opus/final-audio-artifacts --verbose
build/linux/client/release/src/glob2 --verify-match test/fixtures/multiplayer/FourSquares1.g2mr --map maps/FourSquares1.map.gz --out artifacts/opus/verify-03f1395
cmp artifacts/opus/verify-03f1395/checksums.txt test/fixtures/multiplayer/FourSquares1.verify-trace.txt
GLOB2_SDL3_PREFIX=$PWD/build/sdl3-opus/prefix scons -j8 release=1 optimized_assets=0 server=0 CXX=g++-13 CC=gcc-13 install INSTALLDIR=$PWD/artifacts/opus/final-install-root BINDIR=$PWD/artifacts/opus/final-install-bin
```

Linux release game/harnesses and installation pass. All 34 selected native cases pass, including all ten sets switching with a running audio callback, variable callback fade timing, failure/silence handling, voice duration, split audio callbacks and legacy 44.1 kHz recording recovery. Production recording tests compare simulation state with/without recording and threaded presentation; all 702 golden checksums match. Installed inventory is exactly 32 Opus music files and nine attribution files, with no Vorbis. The installed executable starts with its development SDL prefix temporarily unavailable. `ldd` has Opus/Ogg and no Vorbis. This machine's SDL pkg-config still adds a development-prefix RUNPATH; this is local installation evidence, not distribution-clean release certification.

```
python3 browser/build.py web-package web-tests
# From browser/:
npx playwright test threading.spec.js recording.spec.js staged-assets.spec.js --grep 'serial audio produces PCM|serial all soundtrack|serial recording segments full|serial interrupted recording recovers' --output=../artifacts/opus/browser-final-serial-results --reporter=list
npx playwright test threading.spec.js recording.spec.js staged-assets.spec.js --grep 'threaded audio produces PCM|threaded all soundtrack|threaded recording segments full|threaded interrupted recording recovers|later packages arrive' --output=../artifacts/opus/browser-final-threaded-results --reporter=list
npx playwright test recording.spec.js --project=chromium --grep 'serial recording segments full' --repeat-each=3 --output=../artifacts/opus/browser-chromium-resize-recheck --reporter=list
npx playwright test recording.spec.js --project=firefox --grep 'threaded interrupted recording recovers' --repeat-each=3 --output=../artifacts/opus/browser-firefox-recovery-recheck --reporter=list
# From repository root:
node --test browser/unit/*.test.js
```

Serial and threaded builds pass. The focused browser runs pass **12/12 serial + 15/15 threaded/background**, across Chromium, Firefox and WebKit. They exercise nonzero playback PCM, settings responsiveness, clean shutdown, every set's staged inventory, recording/resizing exports and recovery of committed fragments. Rechecks pass **3/3 Chromium resize + 3/3 Firefox threaded recovery** with the default renderer. The earlier Firefox recovery intermittency was not reproduced at this revision; these results do not establish a universal driver fix. Browser JavaScript tests: 42/42.

All 12 unique primary MP4 exports have 48 kHz AAC and zero stream-start offset. One serial Chromium resize export, taken while threaded compilation was still running, has an audio tail 279 ms shorter than video; this is retained rather than hidden. The three unloaded repeats align audio/video durations within 1 ms. Recovery exports differ by no more than 14 ms of AAC tail rounding. This is timestamp/codec evidence, not human listening approval or a guarantee of continuous audio during arbitrary stalls. [Primary probes](browser-final-probes.json) and [repeat probes](chromium-resize-recheck-probes.json) accompany the MP4s.

The small native/serial/threaded decoder smoke also passes with a loop-cropped 4813-frame stream whose large pre-skip/end trim are verified by libopusfile, plus seeking, retired Vorbis rejection and damaged packets. Logs are `loop-decoder-*.log`. The unchanged transport listener regression that previously failed hosted CI now passes 13/13 locally after the main-branch repair; command: build the `wss-listener-test` and `wss-transport-test` targets with the native flags above, then `python3 -m unittest discover -s test/transport -p test_listener.py`.

## Coverage limits and acceptance

Final-revision execution is Linux and all three desktop browser engines. Windows/macOS/iOS, Android devices, and physical audio-device listening are not exercised locally at this revision. Previous-revision hosted Windows/macOS/Linux/Android builds and local Android arm64 game/harness builds are historical evidence in the parent README, not a claim of final-platform validation. Hosted full CI is requested and may still be running. No slow full engine suite, store/signing workflow, or distribution release certification is claimed. Earlier unrelated image-export failures and historical hosted browser/WSS failures remain in the parent evidence/comment.

Maintainer acceptance and listening/play review: pending review; not inferred from automated checks. The implementation and attached local evidence are ready for that review.
