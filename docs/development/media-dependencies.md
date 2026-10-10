# Recording and music dependencies

The recording build and native music playback use pinned codec inputs and bounded buffering. Asset authoring is described in the [music pipeline](../assets/music-pipeline.md).

## Embedded recording dependencies

Client builds compile a pinned minimal FFmpeg/x264 stack through
`scons/recording_dependencies.py`; the source SHA-256 lock is
`scons/recording-versions.json`. Native builds need NASM on x86 targets and
`pkg-config`; Linux builds with `libva` development headers include VAAPI. NVIDIA
headers are pinned with the other sources. Mobile archives use the target compiler,
ABI and SDK, including assembly flags; browser archives use standalone wasm32
SIMD without pthreads. Codec optimization flags remain confined to these archives.

Installed archive hashes, source inputs, recipe, compiler, assembler version, target, SDK and feature
flags form the recording cache identity. A mismatching cache is rebuilt rather
than reused. Release source distributions contain the pinned original archives
under `third_party/recording-sources/`, allowing offline rebuilds. Packages include
codec license notices and configuration. Recording capture has a
`recording.capture` performance scope; simulation and replay checksums must match
with recording enabled and disabled. See [gameplay footage](../features/gameplay-recording.md).

## Music encoding

Runtime music uses Ogg Opus (`.opus`), stereo at 48 kHz, with a 48 kbps VBR
target for the whole stereo stream. Vorbis music is no longer supported. Music
sets retain their directory names and contain `a1.opus`, `a2.opus`, and `a3.opus`.
The three tracks must decode to exactly equal positive frame counts, with one
logical stream each, so mood changes remain aligned. Intro and menu music live
in `data/zik/intro.opus` and `data/zik/menu.opus`.

Use `python3 tools/encode_music.py INPUT OUTPUT.opus` to encode from PCM masters
or convert an existing custom Vorbis track. It uses FFmpeg/libopus with
`-b:a 48k -vbr on -application audio -compression_level 10 -ar 48000 -ac 2`,
resets audio timestamps to a zero-based 48 kHz sample clock, fully decodes the
result for validation, and records the recipe and encoder
version beside the output when `--metadata PATH` is supplied. Convert all three
tracks before installing a custom set. Prefer encoding procedural music directly
from rendered PCM; existing static music may be transcoded once. Encoding tools
are development dependencies, not runtime requirements.

The mixer and gameplay recorder share a 48 kHz PCM rate. Voice packets retain
the existing Speex format and are resampled for playback. Music fade duration is
preserved from the previous 44.1 kHz mixer. Browser builds compile checksum-pinned
Opus, opusfile and Ogg libraries separately for serial and threaded runtimes;
opusfile HTTP support is disabled. Native/mobile builds use their package-managed
opusfile dependencies with libogg retained.

## Buffered music playback

`SoundMixer` is an application-thread facade. Its value-only controls, snapshots
and diagnostics live in `MusicTypes.h`; UI callers do not include decoder or queue
internals. Native playback owns one dedicated producer thread; browser playback
uses a separate Wasm decoder worker. Both run
`Music::Producer`, retaining the existing Opus timeline, loop handling, mood
selection and fixed-point fades. Loading, replacing, seeking and decoder cleanup
happen outside the device callback. Preview screens send typed controls and read
consumed playback snapshots instead of locking SDL or owning live decoders. Preview
session tokens prevent an old screen from controlling or closing a newer preview.

The producer maintains 24 blocks of 1,024 stereo frames (512 ms at 48 kHz), refills
at 20 blocks (427 ms), and cannot exceed 48 blocks. Native output consumes a single-producer,
single-consumer ring without waiting for gameplay or decoder locks. Volume and
mute are applied at consumption. Native voice decoding stays on the application
thread and publishes bounded PCM to separate per-player rings; the music look-ahead
does not add voice latency. The producer requests high scheduling priority, but
failure to obtain it is supported and is not an audio initialization failure.
Set `GLOB2_AUDIO_THREAD_PRIORITY=0` to qualify ordinary-priority production.

Mood requests affect future prepared samples, normally within one second. A
request during an existing fade still waits for that fade to complete; rapid
requests coalesce to the latest mood. Replacement and preview controls use queue
generations to reject obsolete samples. Preview pause retains the queue and partial
block; its clock freezes at consumption and resumes without skipping look-ahead
music. Seek invalidates the old generation even while paused. A real underrun fades
out over five milliseconds and resumes with a fade after refilling; it never loops
a stale block.
No finite queue can cover indefinite OS/browser audio-thread starvation.

Loading and replacement on native playback synchronously wait for the producer to
finish preparation; the audio callback continues consuming the old queue meanwhile.
Only a successful replacement invalidates those samples. Routine mood and preview
controls coalesce and never make the callback wait. Decoder ownership and destruction
stay with the producer; only the consumer advances the queue read cursor.

`SoundMixer::diagnostics()` exposes buffered and consumed frames, underruns,
starvation frames, maximum producer render time, callback time, and observed mood
command latency. Browser diagnostics are available through `Module.glob2Music`.
Do not log from the device callback. Queue diagnostics and dummy audio tests cover
application supply; device-loopback capture and listening are separate evidence.
