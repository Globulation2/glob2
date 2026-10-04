# Windows recording and LAN departure repair

Tested product revision: `64efcdacc80c03e55d27b974bbb002bbd5b284c0`, clean, based on `8bc1b897ff7b30b094d35dd86c3230828988c457`. Ubuntu 26.04 x86_64, GCC 15.2.0, C++20 release/O3, SDL 3.4.16; recording dependencies built from the repository pins. FFmpeg `/usr/bin/ffmpeg` enables decoded-PCM and media validation. The provenance log records exact flags and source tree hash.

Commands (repository root):

```sh
GLOB2_SDL3_PREFIX=/home/bradley/glob2-verify/integrator/sdl3/prefix scons release=1 -j8 tests
LD_LIBRARY_PATH=/home/bradley/glob2-verify/integrator/sdl3/prefix/lib GLOB2_TEST_FFMPEG=/usr/bin/ffmpeg python3 test/run_tests.py --binary unit --build-dir build/linux/client/release --junit artifacts/ci-repair/windows-final64-unit.xml
LD_LIBRARY_PATH=/home/bradley/glob2-verify/integrator/sdl3/prefix/lib GLOB2_TEST_FFMPEG=/usr/bin/ffmpeg python3 test/run_tests.py --binary engine --build-dir build/linux/client/release --filter 'LanMatchHarness/*' --filter 'GameplayRecording.Integration/*' --filter 'TurnEngineHarness/the committed*' --exclude-tag benchmark --junit artifacts/ci-repair/windows-final64-engine.xml
python3 -m unittest discover -s test -p test_run_tests.py
```

Results: 707 unit cases pass, no skips; eight engine cases pass, no skips; 36 runner contracts pass. Engine coverage includes real WSS LAN drop/restart/host departure with all clients' per-tick checksums compared, delayed asynchronous output, destroyed-room timeout diagnostics, three production recording integrations (two compare per-tick state), and the unchanged committed golden match. Unit coverage includes exclusive index collision preservation and decoded PCM timing/content. The LAN latency benchmark is deliberately omitted: shutdown and publication are the changed boundaries, not steady-state latency or simulation. Local testing does not establish Windows/macOS/browser compatibility; the requested hosted matrix supplies those results separately.

Before Windows evidence comes from run 37178754963, source 72b5a8e6b608face92e215d69a4f293e3cba46ed, Windows/MinGW GCC 16.2.0. Nine recording unit failures and the recording integration fail opening session indices. The LAN crash stack identifies timeout-reporting access to a destroyed guest room. The retained network summary shows clean host quit at tick 665, no mismatches, and guest restart reconnection; the test subsequently waits ten seconds for guests to finish. LanLink previously treated its own empty queue as drained while WebSocket writes remained pending; the regression case now checks the latter explicitly. Raw crash bytes are preserved, including bytes that cp1252 console printing could not encode.

A contended local encoder startup also exposed an independent PCM test assumption (before XML retained). The slow-start MP4 has audio start time 0.560s and video start time zero. Ordinary raw decoding strips the initial timestamp gap; `aresample=first_pts=0` restores that silence for comparison on the video clock. The retained aligned PCM/validation JSON meets the original size/content assertions and has 0.884ms onset error (50ms permitted). No encoder behavior or synchronization assertion was relaxed.
