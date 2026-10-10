# Assets verification

Focused regression scenarios and commands. Start with the [native test guide](../../../test/README.md) for building, isolation and runner selection.

## Gameplay recording

The `GameplayRecording` unit suite checks lifecycle failures, output protection,
and decoded callback audio bursts. `GameplayRecording.Integration` compares serial
and threaded recordings against unrecorded per-tick baselines.
Set `GLOB2_TEST_FFMPEG=ffmpeg` to also encode real video/audio and chapter fixtures.
Run `python3 test/test_recording_tool.py` for manifest selection and extraction
argument tests. See [gameplay recording](../../features/gameplay-recording.md).


## Soundtrack selection

`MusicSet/*` exercises installed soundtrack discovery, atomic decoder failure,
queued mood replacement, live dummy-audio switching, muted changes and saved
preferences through the current in-game dialog API. `Settings/*` also covers the
Audio selector and captures its layout. The mixer selection regression suite is
`SoundMixerTrackSelection/*`. These display cases run in isolated runner processes.
Only Original ships, so `MusicSet/*` builds its extra valid and broken sets from copies
of the shipped Oggs in the disposable profile.

### Audio buffering and CPU contention

`MusicProducer/*` compares file/memory decoding and PCM, trimmed loops, atomic
replacement, queued moods, and preview ownership. `MusicBuffer/*` covers bounded
capacity, concurrent wraparound, 100/250/400 ms producer stalls, generation
invalidation and starvation recovery. `SoundMixerTrackSelection/*` retains the
fade/selection regressions and exercises muted startup, ordinary-priority production,
preview session rejection, pause/resume sample continuity, paused seek, operation
without a running device, and native worker shutdown.
Run these together with `MusicSet/*`, `CommunityMusic*/*`, and `GameplayRecording*/*`:

```sh
python3 test/run_tests.py --filter 'Music*/*' --filter 'SoundMixer*/*' --filter 'CommunityMusic*/*' --filter 'GameplayRecording*/*'
node --test browser/unit/*.test.js
(cd browser && npx playwright test tests/music-audio.spec.js tests/music-game.spec.js tests/recording.spec.js)
```

The browser suite uses the production Wasm producer and worklet at 44.1 and 48 kHz,
checks visibility/resume, and blocks the host for two seconds with shared transport
or 400 ms with MessagePort transport. A browser build without SharedArrayBuffer
reports the shared cases as unavailable; its MessagePort cases still run. Unit tests
also exercise pause/resume and reset command ordering. Shared cases use real server
isolation headers: WebKit does not expose SharedArrayBuffer for a
Playwright-synthesized response, even when it reports cross-origin isolation.
`music-game.spec.js` checks the application bridge and mute in both game runtimes.
Set `GLOB2_MUSIC_TEST_ARCHIVE` to a valid release ZIP to also test imported preview
controls; without that fixture those preview cases are explicitly skipped.
`recording.spec.js` unmutes music and checks that consumed nonzero PCM reaches the
recorder in each runtime, then exports completed MP4s. File existence alone does
not establish that an audio track contains music. Use distinct Playwright
`--output` directories for concurrent runs.

For a native ten-minute supply run, use the benchmark directly (the ordinary test
runner excludes `[benchmark]` cases). `GLOB2_AUDIO_THREAD_PRIORITY=0` disables the
best-effort priority promotion. The test chooses moods every two seconds and emits
`AUDIO_STRESS` queue, starvation, render, callback and response measurements:

```sh
GLOB2_AUDIO_THREAD_PRIORITY=0 GLOB2_AUDIO_STRESS_SECONDS=600 build/linux/client/release/test/glob2-engine-tests --test-case='native music supply*'
```

The harness defaults to dummy SDL audio; set `SDL_AUDIODRIVER` explicitly for a
real output device. Apply CPU affinity and independent CPU/memory competitors as
appropriate, recording those settings with the result.

For browser ten-minute contention runs, serve the unversioned build directory
`build/emscripten/client/release`, then run:

```sh
node browser/benchmarks/audio.cjs http://127.0.0.1:8770 allcore 600
node browser/benchmarks/audio.cjs http://127.0.0.1:8770 memory 600
taskset -c 0 node browser/benchmarks/audio.cjs http://127.0.0.1:8770 singlecore 600
```

Choose an allowed CPU for `taskset`. The single-core run shares that CPU with two
busy workers; memory mode uses four 128 MiB arrays; all-core mode uses the allowed
CPU count. Each run emits JSON diagnostics and fails on new application starvation.
Record browser/OS versions, priority, affinity, concurrent workloads, exact source
revision and device conditions with the result. These tests do not measure hardware
underruns or replace loopback capture/listening. Keep logs in `artifacts/` and link
review evidence externally when preparing a PR.
