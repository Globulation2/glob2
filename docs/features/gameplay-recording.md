# Gameplay footage

Glob2 can record menus, loading screens, gameplay, in-game dialogs, and
final statistics as compressed MP4 footage. The same recorder supports players,
replay capture, LAN and online multiplayer, and internal feature demonstrations.
It records the local client's visible perspective and mixed game audio, including
received voice chat. It does not capture a new microphone input or other windows.

## Recording

Recording uses embedded H.264 and AAC encoders; no FFmpeg executable is needed.
Native builds prefer the platform hardware encoder and fall back to x264. Browser
builds lazily load a dedicated recording worker, prefer WebCodecs after testing the
actual configuration, and fall back to the same embedded software pipeline.

**Settings > Recording** shows recording controls, the hotkey and the output
folder. The in-game menu offers **Start recording / Stop recording**, and the
**Start or stop recording** hotkey (default **Ctrl+Shift+R**, rebindable under
**Settings > Controls**) works on every screen. Encoder and storage errors appear
in the recording controls. Each UI start creates a unique MP4 in
`videoshots/` beneath the current user profile. The window title reports recording,
finalization, or failure; that status is outside the captured image. Capture
errors also appear in the recording controls and application log.

For a complete session, start recording from the command line:

```sh
glob2 -vs feature-demo
glob2 --record artifacts/footage/feature-demo.mp4 --record-fps 30 --record-encoder software
```

`-vs <name>` now produces `videoshots/<name>.mp4` in the profile, replacing its
historical numbered BMP files. GPU rendering is supported; `-G` is unnecessary.
`--record` paths are ordinary filesystem paths, relative to the working directory
unless absolute. Existing output files and reserved recording names are never
replaced. Recording finishes when stopped or when the application exits normally.

| Option | Default | Meaning |
| --- | --- | --- |
| `--record-fps` | 30 | Fixed recording rate, 1–240 FPS |
| `--record-crf` | 23 | Software H.264 quality, 0–51 |
| `--record-chapter-ticks` | 10000 | Gameplay chapter era length |
| `--record-encoder` | auto | Hardware preference, or `software` for reproducibility |

`--record-ffmpeg` and `--record-size` are obsolete and produce migration errors.
Recording captures the full rendered framebuffer, including HiDPI pixels. Odd
widths and heights are padded to even dimensions without scaling. Resolution
changes settle for 250 ms before closing the current file and starting a numbered
`.part0002.mp4` segment. Frame rate and quality remain fixed during a session.

Output frame rate does not change game rendering cadence or simulation speed.
When the game presents fewer unique frames, the recorder repeats its latest
frame. Missing audio is filled with silence. Long execution suspension preserves
a timestamp gap and resumes without a catch-up burst. Background execution still
depends on the operating system and browser. Readback and copying have a cost;
bounded queues reject excess input and report dropped frame/audio counters.

The **Recordings** screen lists completed and interrupted files. Export video,
metadata and events separately; recovery remuxes available completed fragments.
Native files remain in the profile's `videoshots/` directory. Mobile recordings
use app-private storage and native document pickers. Browser recordings stream
to OPFS outside IDBFS and export a file-backed download; unavailable or exhausted
storage produces an error. Do not clear browser site storage before exporting.

## Chapters and extraction

Each completed segment has three files:

- `<name>.mp4`: compressed video/audio with embedded navigation chapters;
- `<name>.mp4.json`: versioned manifest with chapter intervals and semantic context;
- `<name>.mp4.events.jsonl`: chronological events, including chapters, pauses,
  speed changes, resizing, capture gaps, and statistics metric selections.

A versioned `<name>.mp4.session.json` index lists completed segments and their
session-relative start times. Per-file manifests retain version 1 and add optional
session, segment, encoder and fallback fields.

Chapter identifiers are occurrence IDs: visiting the same screen twice produces
two intervals. Stable `screen` and `dialog` identifiers are independent of language
and compiler. Phases are `menu`, `loading`, `gameplay`, `dialog`, and `results`.
Network lobby tabs have distinct identifiers, including `multiplayer_game`.

Gameplay chapters use absolute half-open tick eras: `[0,10000)`, `[10000,20000)`,
and so on. A loaded game starting at tick 15000 begins in the second era. Each
match has a recording-local identifier; its manifest chapters retain mode, map,
local team, and observed ticks. No chapters are invented for eras that have no
recorded frames. Returning from a dialog creates another occurrence of the
current gameplay era.

Times use integer microseconds relative to the recorded video, not a conversion
from simulation ticks. Chapter transitions take effect on the first recorded
frame showing the context. Pauses and replay speed changes therefore keep both
timestamps and tick labels meaningful. Structured metadata is authoritative for
automation; embedded chapter presentation varies between video players.

```sh
python3 tools/recording.py list artifacts/footage/feature-demo.mp4
python3 tools/recording.py list artifacts/footage/feature-demo.mp4 --json
python3 tools/recording.py extract artifacts/footage/feature-demo.mp4 \
  --phase gameplay --match 1 --era-start 10000 --output-dir artifacts/clips
python3 tools/recording.py extract artifacts/footage/feature-demo.mp4 \
  --phase results --output-dir artifacts/results
python3 tools/recording.py extract artifacts/footage/feature-demo.mp4 \
  --screen main_menu --format webm --output-dir artifacts/website
```

Filters combine with AND; repeating a filter selects any of its supplied values.
Each selected interval becomes a separate clip, with a JSON sidecar retaining
source chapter context. Default MP4 extraction re-encodes for frame-accurate
boundaries. WebM exports use VP9/Opus and need those FFmpeg encoders. `--copy`
provides faster, approximate cuts at keyframe boundaries for MP4. `--dry-run`
prints argument arrays without creating output files. The tool never overwrites
existing clips or sidecars.

## Failures and implementation

During capture, `<name>.mp4.recording/` reserves the output and holds a fragmented
MP4 containing both streams, an append-only event journal and incomplete metadata.
Alternating metadata checkpoints preserve the previous valid copy when storage
fills or a checkpoint write is interrupted.
Finalization remuxes compressed packets into a fast-start MP4 with chapters.
Native publication uses no-replace hard links; browser publication commits a
completion marker after streaming bounded chunks into final OPFS files. Failures
retain staging files and diagnostics. Forced termination can leave a partial last
fragment, so recovery is best effort.

The `libgag` session controller owns lifecycle, the shared monotonic timeline,
three-frame capture budget, segments and semantic context. Video encoders return
packets asynchronously through an interface with no FFmpeg types. AAC, audio
alignment, MP4 writing and storage belong to the worker. Audio callbacks copy into
preallocated storage with a nonblocking lock; they neither allocate nor wait.
Workers handle color conversion, encoding and file operations. Normal stop is
cooperative; native application shutdown joins outstanding workers.

Software uses x264 ultrafast, CRF 23, one thread, zero latency, no B-frames and a
two-second keyframe interval. AAC is stereo at 44.1 kHz and 192 kbps. Hardware
settings stay inside the adapters: low latency, no B-frames, two-second keyframes
and variable bitrate starting at `max(1 Mbps, width × height × fps × 0.12)`.

| Platform | Hardware preference |
| --- | --- |
| macOS / iOS | VideoToolbox |
| Android | MediaCodec |
| Windows | Media Foundation, then NVENC |
| Linux | VAAPI (when built with libva), then NVENC |
| Browser | WebCodecs with advisory hardware preference |

The pinned hashes are in `scons/recording-versions.json`; the minimal dependency
recipe and cache validation are in `scons/recording_dependencies.py`. Only H.264
encoders, AAC, MP4 muxing, MOV demuxing and required conversion/prerequisites are
enabled. No decoders, filters, tools or network protocols are included. Release
source archives include the original dependency archives for offline rebuilds,
and packages include their license notices. Dependencies retain native/WASM SIMD;
encoder optimization flags do not alter simulation compiler flags.

Recording has no serialized state and changes no save, replay, or network version.
Changes to recording still require simulation-checksum comparisons and platform
coverage reporting, as described in the [development guide](../development/headless-replays.md).

For verification, build unit tests; an external FFmpeg is used only for independent decoding:

```sh
scons release=1 server=0 unit-tests
GLOB2_TEST_FFMPEG=ffmpeg build/darwin/client/release/test/glob2-unit-tests -ts=GameplayRecording
python3 test/test_recording_tool.py
scons release=1 server=0 engine-tests
GLOB2_TEST_FFMPEG=ffmpeg build/darwin/client/release/test/glob2-engine-tests -ts=GameplayRecording.Integration
GLOB2_TEST_FFMPEG=ffmpeg GLOB2_TEST_FFPROBE=ffprobe \
  GLOB2_RECORDING_FIXTURE=artifacts/path/to/recording-sections.mp4 \
  python3 test/test_recording_tool.py
scons release=1 server=0 recording-multiplayer-test
python3 test/run_recording_multiplayer.py \
  build/darwin/client/release/test/recording-multiplayer-peer \
  --output artifacts/multiplayer-recording
```

The encoder unit suite also decodes consecutive callback chunks and checks fatal
capture errors and late output collisions. The integration suite compares both
serial and threaded capture against unrecorded per-tick baselines. The optional
Python media checks inspect codecs, embedded chapters, chronological events, and
decoded first frames of extracted clips; use the generated `recording-sections-*`
fixture for its documented palette.

The multiplayer fixture runs two real LAN clients on the turn protocol through a
match and results using the production session mode. It retains both videos and compares every executed
tick’s checksum, written directly from each client’s lockstep callback.

Use the equivalent build path on Linux or Windows. Test recordings and review
evidence belong in ignored `artifacts/`, not committed documentation. Check
rendered footage and multiplayer responsiveness through maintainer playtesting;
codec inspection alone does not establish visual quality or gameplay feel.

For matched local performance qualification, use the busy saved battle and keep
rendering, camera movement, audio, runtime, dimensions and warmup identical:

```sh
node browser/benchmarks/recording.cjs http://127.0.0.1:8770 artifacts/recording-benchmark
GLOB2_SDL3_PREFIX=build/sdl3-ci/prefix scons release=1 software-render-benchmark
PROFILE_SAVE=games/gd-bigarena-long.game.gz PROFILE_RECORD=off PROFILE_SECONDS=20 \
  PROFILE_PAN=1 PROFILE_AUDIO=1 PROFILE_WARMUP=240 \
  build/darwin/client/release/test/SoftwareRenderBenchmark -s 1920x1080
PROFILE_SAVE=games/gd-bigarena-long.game.gz PROFILE_RECORD=artifacts/qualification.mp4 \
  PROFILE_SECONDS=20 PROFILE_PAN=1 PROFILE_AUDIO=1 PROFILE_WARMUP=240 \
  build/darwin/client/release/test/SoftwareRenderBenchmark -s 1920x1080
```

The browser fixture runs recorded/unrecorded pairs in both runtimes at 720p,
1080p and a double-density display, with three repetitions. Its JSON retains the
fixture hash, frame times, CPU, memory, recording metadata and dropped input.
The native rendering fixture isolates a loaded battle scene without advancing
simulation; active battle costs still require the browser fixture or live-game
profiling. Native telemetry includes the `recording.capture` readback/submission
scope. Use `PROFILE_RECORD_SOFTWARE=1` for x264 and `PROFILE_NATIVE_DISPLAY=1`
for native display density; `PROFILE_VISIBLE=1` keeps the benchmark window visible.
Do not measure while builds or unrelated workloads compete for CPU. Qualify
realtime recording with no encoder-pressure drops and less than 5% game FPS
regression. Report devices and resolutions that miss those targets; recording
retains the selected full resolution and frame rate. Mobile hardware and sustained
thermal qualification require physical devices.
