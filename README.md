# Opus migration review evidence for PR #725

Implementation revision: `ec44a58b808fefc2b7832fd68b14fadb7ab43c76`.
Base: `75c7426224496c9758c23d3796f011e605dd4d53`.
Master refreshed during final verification: `4bb472a5dfb5e3d7cfa38c983b804fd64bc70613`; intervening online-link and mesh changes do not change the audio integration.

Environment: Ubuntu 26.04.1 x86_64, GCC/G++ 13.4.0, Python 3.14.4, FFmpeg 8.0.1, native libopusfile 0.12 and libopus 1.5.2. SDL3 family built from repository pins. Browser decoder versions and checksums are in the PR's `scons/opus-versions.json`; browser SDK is the repository-managed SDK. Android uses the managed NDK 28.2.13676358 and vcpkg dependencies, arm64-v8a/API 24, release.

## Commands and results

Native release game and harnesses:
```
GLOB2_SDL3_PREFIX="$PWD/build/sdl3-opus/prefix" scons -j10 release=1 server=0 CXX=g++-13 CC=gcc-13 tests build/linux/client/release/src/glob2
GLOB2_TEST_FFMPEG=/usr/bin/ffmpeg python3 test/run_tests.py --filter 'MusicSet/*' --filter 'SoundMixerTrackSelection/*' --filter 'GameplayRecording/*' --filter 'RecordingSession/*' --filter '*PlayerVoice*/*' --filter 'GameMusicController/*' --junit artifacts/opus/audio-final-tests.xml --artifacts artifacts/opus/final-test-artifacts --verbose
python3 test/run_tests.py --binary unit --quick --junit artifacts/opus/unit-tests.xml --artifacts artifacts/opus/final-unit-artifacts
python3 test/run_tests.py --filter 'MobileDocuments/*'
build/linux/client/release/src/glob2 --verify-match test/fixtures/multiplayer/FourSquares1.g2mr --map maps/FourSquares1.map.gz --out artifacts/opus/verify-ec44a58
cmp artifacts/opus/verify-ec44a58/checksums.txt test/fixtures/multiplayer/FourSquares1.verify-trace.txt
ldd build/linux/client/release/src/glob2
```
Final revision: 31 focused music/voice/recording cases and the mobile document test passed. Earlier full selected quick unit pass: 743 cases; subsequent changes add repeated-mood assertions, SDK wrappers, browser playback coverage and a missing mobile test double. Final relevant cases refreshed; no claim of a full slow engine suite. All 702 golden per-tick checksums match. No simulation/save/network/replay version changed. Native linked dependencies contain opusfile/opus/ogg and no Vorbis.

Tooling: 3 music encoding tests, 14 optional-asset tests (one unrelated skip), 25 mobile build tests, 17 CI policy tests and 42 browser JavaScript cases pass. The broader 297 build-system cases have four image-export failures (two failures/two errors), reproduced in the unmodified master snapshot; see baseline and broad suite logs. Those are not evidence of passing packaging export coverage.

```
python3 -m unittest discover -s test/build_system -p test_music_encoding.py
python3 -m unittest discover -s test/build_system -p test_web_assets.py
python3 -m unittest discover -s test/build_system -p test_mobile.py
python3 -m unittest discover -s test/build_system -p test_ci_policy.py
node --test browser/unit/*.test.js
scons target=android arch=arm64-v8a release=1 android-tests -j12
python3 browser/build.py web-package web-tests
```
Android release game and both harnesses compile/link. No Android device/emulator was available for execution. The native and separate serial/threaded WebAssembly decoder smoke source is attached; all pass trimmed timeline, end seek, rejected Vorbis and damaged packet completion. Runtime music inventory fully decodes with libopusfile, matches its trimmed total frames, and contains only five `.opus` files. Original trio lengths are exactly equal: 2,566,809 frames.

## Size and listening review

[Per-track sizes](sizes.md), [conversion metadata](static-conversion.json), [decoded asset inventory](asset-validation.json).

Total bundled music falls from 3,779,736 to 1,510,549 bytes (60.0%). Encoding is stereo 48 kbps VBR total, application audio, compression level 10, 48 kHz; timestamp reset after resampling avoids the original a3 granule offset. No added loudness normalization. `listening/` contains matching 12-second lossless before/after clips for all five tracks. These are review samples, not a claim that a human has listened to and approved every full track, loop boundary or mood transition. Full runtime assets remain in the PR for that listening/play review.

## Pending soundtrack integration

PR #707 is explicitly parked, not scheduled for merge. Its unapproved sets are excluded. `parked-generator-opus.patch` adapts that branch's renderer/WAV/install tools to 48 kHz/direct PCM-to-Opus; isolated tooling tests have four passes and one unavailable-soundfont skip. This patch is evidence for later integration, not code installed by PR #725. No active new soundtrack sources have landed on master at final inspection.

## Coverage limits

Windows, macOS, iOS and Android device execution are not locally verified. Full hosted checks were explicitly requested and are still running/queued; their status must be evaluated independently. No claim of a successful complete cross-platform matrix. Listening and maintainer play review remain outstanding. This PR stays draft.
