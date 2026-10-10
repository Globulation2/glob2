# Browser music playback

Music playback has its own decoder worker and AudioWorklet, independent of the game loop. [Browser architecture](implementation.md) describes the host boundary.

### Music playback

Both game runtimes use a separate Wasm decoder worker and an AudioWorklet. The
application sends assets and controls through `browser/Audio.cpp` and the UI host
in `browser/audio.js`; ongoing playback never calls the application mixer or
waits for its event loop.

| Owner | Responsibilities | Must not do |
| --- | --- | --- |
| Application / UI host | Validate and transfer compressed assets, coalesce controls, activate and suspend the device, forward capture | Forward ongoing music PCM between worker and worklet |
| Decoder worker | Own `Music::Producer`, decode/seek/transition, publish prepared blocks | Access game Wasm or its filesystem during playback |
| AudioWorklet | Consume prepared PCM, resample continuously to the device rate, apply gain and recovery ramps, publish bounded capture/diagnostics | Decode, perform I/O, wait for locks or allocate unbounded queues |

The worker runs the same `Music::Producer` as native playback. It prepares
1,024-frame 48 kHz stereo blocks, maintaining a 24-block (512 ms) target with a
20-block (427 ms) refill threshold and a hard 48-block capacity. Volume and mute are
applied at consumption, so they do not wait for the prepared queue to drain.
The half-second target rounds up to whole blocks; the refill threshold retains
a 400 ms producer-stall cushion when at least 20 blocks remain queued.

Isolated browsers with SharedArrayBuffer use an atomic ring; other browsers use
a direct MessageChannel with recycled transferable buffers. Shared transport has
64 physical slots for wraparound and the same 48-block logical capacity. Each
transferable buffer is a credit: the worker can publish it again only after the
worklet returns it, including when a generation change discards stale PCM.
The consumer alone advances the read cursor; resets never overwrite it from the
producer. Control delivery has one command in flight and coalesces repeated mood
and preview requests while preserving order between different controls.

Some WebKit builds delay MessagePort delivery during page long tasks. Those
builds can tolerate buffered stalls but cannot cover an arbitrary multi-second
UI blockage without shared transport. Genuine starvation enters explicit refill
mode, fades out once, and waits for the target before fading back in. It never
loops stale audio. Generations separate replacements, preview sessions and seeks;
a failed replacement retains the previous valid music. Preview pause freezes the
consumer and retains its partial block, so resume continues at the next sample.
Reset preserves the consumed timeline: the worker briefly pauses consumption and
requests that position before replacing prepared PCM. Generation checks reject
stale replies; a one-second acknowledgement timeout rejects only that reset and
releases the control queue. It never seeks to a guessed decoder-ahead cursor.

The UI owns device lifecycle. Activation starts in the initiating gesture before
asynchronous worklet loading, and a refused activation may be retried by a later
gesture. Hidden tabs suspend both context and production; visible tabs refill
before audible consumption resumes. Teardown closes the context and terminates
the worker. Initialization failure reports unavailable audio without stopping the
game. Package `music-worker.js`, `music-output.js`, `music-runtime.js` and
`music-runtime.wasm` with both game runtimes. The static packager includes these
files and Opus license notices, versions worker/worklet/Wasm URLs together, and
checks all gzip sidecars; a partial backend cannot produce a release package.

Recording receives bounded copies of consumed, gain-adjusted 48 kHz PCM after
starvation handling. Its timestamps follow consumption rather than the decoder's
look-ahead. Eight capture buffers provide credits; if recording cannot return
them in time, capture drops samples without blocking playback. Credits are
returned even if the recording bridge fails.

`Module.glob2Music.status` reports queued, consumed and starvation frames,
underruns, maximum render time, command latency, captured frames and dropped
capture frames. `.transport` identifies the selected transport. Compare counters
over an observation interval; startup/refill and actual starvation are different
states. These counters establish application supply, not hardware continuity.
See [audio verification](../development/testing/assets.md#audio-buffering-and-cpu-contention)
for deterministic, browser and contention tests.
