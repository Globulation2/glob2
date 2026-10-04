# Latest integration

The complete nine-set migration is verified at `03f1395b99be79de2e23be711d730b35a5453162`, rebased on `11124f736befac9880dfb7b3448df79129ed9206`. See [the final integration evidence](integration-03f1395/README.md) for all 32 tracks, 38.51% size reduction, current validation and review audio. The material below is retained historical evidence for the earlier five-track revision.

# Opus migration review evidence for PR #725

Implementation revision: `718704ed228fb2758f6785f131d210f8ba613020`.
Base: `75c7426224496c9758c23d3796f011e605dd4d53`.
Master refreshed during final verification: `0a05b533fe9a3d2141e1e447da21c8ba6066e12b`; intervening online-link, mesh, skin and gradient changes do not change the audio inputs. The overlapping native install RPATH update is exercised separately below. The merge tree is conflict-free.

Environment: Ubuntu 26.04.1 x86_64, GCC/G++ 13.4.0, Python 3.14.4, FFmpeg 8.0.1, native libopusfile 0.12 and libopus 1.6.1. SDL3 family built from repository pins. Browser decoder versions and checksums are in the PR's `scons/opus-versions.json`; browser SDK is the repository-managed SDK. Android uses the managed NDK 28.2.13676358 and vcpkg dependencies, arm64-v8a/API 24, release.

## Commands and results

Native release game and harnesses:
```
GLOB2_SDL3_PREFIX="$PWD/build/sdl3-opus/prefix" scons -j10 release=1 server=0 CXX=g++-13 CC=gcc-13 tests build/linux/client/release/src/glob2
GLOB2_TEST_FFMPEG=/usr/bin/ffmpeg python3 test/run_tests.py --filter 'MusicSet/*' --filter 'SoundMixerTrackSelection/*' --filter 'GameplayRecording/*' --filter 'RecordingSession/*' --filter '*PlayerVoice*/*' --filter 'GameMusicController/*' --junit artifacts/opus/audio-final-tests.xml --artifacts artifacts/opus/final-test-artifacts --verbose
python3 test/run_tests.py --binary unit --quick --junit artifacts/opus/unit-tests.xml --artifacts artifacts/opus/final-unit-artifacts
python3 test/run_tests.py --filter 'MobileDocuments/*'
build/linux/client/release/src/glob2 --verify-match test/fixtures/multiplayer/FourSquares1.g2mr --map maps/FourSquares1.map.gz --out artifacts/opus/verify-718704e
cmp artifacts/opus/verify-718704e/checksums.txt test/fixtures/multiplayer/FourSquares1.verify-trace.txt
ldd build/linux/client/release/src/glob2
```
Final revision: 31 focused music/voice/recording cases, three production recording integration cases, and the mobile document test passed. Fade timing and aligned PCM positions are checked at 127, 1024 and 4096 frames per callback. Recording integration compares per-tick simulation state with and without capture, including an in-game menu pause and threaded scene capture. Earlier full selected quick unit pass: 743 cases; subsequent changes add repeated-mood assertions, SDK wrappers, browser playback coverage and a missing mobile test double. Final relevant cases refreshed; no claim of a full slow engine suite. All 702 golden per-tick checksums match. No simulation/save/network/replay version changed. Native linked dependencies contain opusfile/opus/ogg and no Vorbis.

Tooling: 4 music encoding/install tests, 14 optional-asset tests (one unrelated skip), 25 mobile build tests, 17 CI policy tests and 42 browser JavaScript cases pass. The broader 297 build-system cases have four image-export failures (two failures/two errors), reproduced in the unmodified master snapshot; see baseline and broad suite logs. Those are not evidence of passing packaging export coverage.

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

Browser game and harness build revision is `468aba1297a36c47360273bc969fa97fc61a4057`; the only later implementation change is expanded native/Android mixer harness coverage, which is not compiled into browser binaries. Browser production inputs are unchanged.

## Browser execution and hosted feedback

The 21 focused browser checks initially produced 20 passes and one Firefox threaded recovery UI timeout (`nav.8` never appeared after opening Settings). Both playback variants pass in all three engines; Chromium and WebKit export and interruption recovery both pass in serial/threaded modes. Firefox serial recovery and both recording exports pass. All eleven produced/exported MP4s have 48 kHz AAC audio; probes and videos are attached.

An exact three-repeat Firefox threaded recovery rerun on the default renderer produced two passes and one startup timeout (`screen=loading` after reload). Its stability is not established. Software-renderer reruns are recorded separately; they do not establish default WebGL recovery stability. The initial compact trace retains all interactions/snapshots/screenshots, with duplicate screenshots sharing image entries and large immutable asset response bodies omitted.

Commands:
```
cd browser
npx playwright test threading.spec.js recording.spec.js staged-assets.spec.js --grep 'audio produces PCM|recording segments full|interrupted recording recovers|later packages arrive'
npx playwright test recording.spec.js --project=firefox --grep 'threaded interrupted recording recovers' --repeat-each=3 --output=../artifacts/opus/browser-recheck-results --reporter=list
GLOB2_TEST_RENDERER=software npx playwright test recording.spec.js --project=firefox --grep 'threaded interrupted recording recovers' --repeat-each=3 --output=../artifacts/opus/browser-software-recheck-results --reporter=list
```

Hosted full run [37230946577](https://github.com/Globulation2/glob2/actions/runs/37230946577) has a failed native WSS job: `test_text_mode_echoes_text_and_rejects_binary` hit `ssl.SSLEOFError` during frame sending. Network/transport code and tests are unchanged by this PR; no claim that a baseline hosted rerun passed. The failure log is attached. Other matrix jobs remain independently running/queued. This is not a green hosted matrix or final maintainer acceptance.

Firefox software-renderer recovery rerun: **3/3 passed**, with 48 kHz AAC verified in the recovered files. Default-renderer stability remains unverified (2/3 reruns passed). Native/software recording integration covers the audio change; neither software success nor a passing retry establishes default WebGL reliability.

The latest master changes SConstruct's install RPATH handling and Windows GUI installer smoke. The merged SConstruct was temporarily exercised against this PR's source using a real `scons -j8 release=1 optimized_assets=0 server=0 CXX=g++-13 CC=gcc-13 install INSTALLDIR=<ignored install-root> BINDIR=<ignored install-bin>`. It installs exactly the five Opus files. The installed executable passes `--version` with LD_LIBRARY_PATH unset and the development SDL prefix temporarily unavailable; linkage contains Opus/Ogg and no Vorbis. Only the shared SConstruct build input is integrated for this test, not unrelated master gameplay/skin changes. Source is restored and the PR working tree is clean. Windows installer smoke remains unavailable locally. The local SDL pkg-config configuration still contributes a development-prefix RUNPATH in addition to $ORIGIN; this is documented in the ELF log, not certified as distribution-clean packaging.

Maintainer acceptance: pending listening/play review and outstanding platform/Firefox-default-renderer coverage; the PR remains draft.
