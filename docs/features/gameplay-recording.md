# Gameplay footage

Desktop Glob2 can record menus, loading screens, gameplay, in-game dialogs, and
final statistics as compressed MP4 footage. The same recorder supports players,
replay capture, LAN and online multiplayer, and internal feature demonstrations.
It records the local client's visible perspective and mixed game audio, including
received voice chat. It does not capture a new microphone input or other windows.

## Recording

Install an FFmpeg executable with `libx264` and AAC encoders and make it available
on `PATH`, or select it with `--record-ffmpeg`. FFmpeg is optional: ordinary play
does not require it. Browser and mobile recording are not supported yet.

Use **Start recording / Stop recording** in the main menu, in-game menu, or final
statistics screen, or press **Ctrl+Shift+R**. Each UI start creates a unique MP4 in
`videoshots/` beneath the current user profile. The window title reports recording,
finalization, or failure; that status is outside the captured image. Capture
errors also appear in the recording controls and application log.

For a complete session, start recording from the command line:

```sh
glob2 -vs feature-demo
glob2 --record artifacts/footage/feature-demo.mp4 --record-fps 30 --record-size 1920x1080
```

`-vs <name>` now produces `videoshots/<name>.mp4` in the profile, replacing its
historical numbered BMP files. GPU rendering is supported; `-G` is unnecessary.
`--record` paths are ordinary filesystem paths, relative to the working directory
unless absolute. Existing output files and reserved recording names are never
replaced. Recording finishes when stopped or when the application exits normally.

| Option | Default | Meaning |
| --- | --- | --- |
| `--record <path.mp4>` | Off | Record a complete rendered session |
| `--record-fps <1..240>` | 60 | Constant output frame rate |
| `--record-size <WxH>` | Native framebuffer | Fixed output canvas |
| `--record-crf <0..51>` | 18 | H.264 quality; lower values retain more detail |
| `--record-chapter-ticks <N>` | 10000 | Gameplay-era width in simulation ticks |
| `--record-ffmpeg <path>` | `ffmpeg` on `PATH` | Encoder executable |

The video uses H.264/YUV420p and AAC stereo audio at 192 kbps. Native dimensions
are fixed by the first captured frame and padded to even numbers. Subsequent
resizes are scaled and letterboxed into that canvas. A HiDPI framebuffer can be
larger than the logical game viewport; use `--record-size` to constrain exports.

Output frame rate does not change game rendering cadence or simulation speed.
When the game presents fewer unique frames, the recording repeats its latest
frame. Pauses, minimization, mute, and multiplayer synchronization waits preserve
elapsed recording time; absent audio becomes silence. The recorder never sends a
network order or pauses the match to wait for the encoder. Native frame readback
and copying still have a cost, especially at high resolutions; bounded queues
skip capture frames under encoder pressure rather than accumulating raw images.

## Chapters and extraction

A completed recording has three files:

- `<name>.mp4`: compressed video/audio with embedded navigation chapters;
- `<name>.mp4.json`: versioned manifest with chapter intervals and semantic context;
- `<name>.mp4.events.jsonl`: chronological events, including chapters, pauses,
  speed changes, resizing, capture gaps, and statistics metric selections.

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

During capture, `<name>.mp4.recording/` reserves the output and holds compressed
video/audio tracks, the append-only event journal, and encoder logs. Finalization
combines compressed tracks and chapter metadata without re-encoding. Successful
completion removes the work directory. If encoding, storage, or finalization
fails, it retains available compressed tracks, logs, and an incomplete manifest
for diagnosis. Abrupt process termination may leave only completed fragments;
recovery is best effort. Keep this directory until useful footage is recovered.

The recorder lives in `libgag` and owns no game state. The shared presentation
boundary submits completed frames before swap/clear. Game-side callers provide
stable screen/dialog identifiers, match context, ticks, and events. The mixer
copies final PCM into preallocated callback storage with a nonblocking lock.
Workers handle encoding, timing, drift/gap correction, and metadata writes.
Video buffering is bounded to three frames, audio to two seconds of samples.
Shell-free subprocess launch uses native POSIX or Windows process APIs.

Recording has no serialized state and changes no save, replay, or network version.
Changes to recording still require simulation-checksum comparisons and platform
coverage reporting, as described in the [development guide](../development/headless-replays.md).

For verification, build unit tests and opt into the real encoder fixture:

```sh
scons release=1 server=0 unit-tests
GLOB2_TEST_FFMPEG=ffmpeg build/darwin/client/release/test/glob2-unit-tests -ts=GameplayRecording
python3 test/test_recording_tool.py
scons release=1 server=0 recording-multiplayer-test
python3 test/run_recording_multiplayer.py \
  build/darwin/client/release/test/recording-multiplayer-peer \
  --output artifacts/multiplayer-recording
```

The multiplayer fixture runs two real LAN clients through a match and results,
retains footage and replays, and compares their full per-tick checksum sidecars.
Use `--ffmpeg /path/to/ffmpeg` when the encoder is outside `PATH`.

Use the equivalent build path on Linux or Windows. Test recordings and review
evidence belong in ignored `artifacts/`, not committed documentation. Check
rendered footage and multiplayer responsiveness through maintainer playtesting;
codec inspection alone does not establish visual quality or gameplay feel.
