// SPDX-License-Identifier: GPL-3.0-or-later
importScripts('music-runtime.js');
// Shared ring protocol (keep aligned in worker and worklet): the worker publishes
// PCM/metadata before Write; the consumer alone advances Read. A generation change
// is acknowledged only after old slots are discarded, before new slots are reused.
const Shared = { Read: 0, Write: 1, Generation: 2, Acknowledged: 3, NeedsPrefill: 4 };
// Numeric values are the Music::Control wire contract in MusicTypes.h.
const Control = { Play: 0, Mood: 1, Seek: 2, Fade: 3, Blend: 4, Audition: 5, Reset: 6 };

let runtime,
  port,
  generation = 0,
  enabled = false,
  hidden = false,
  outstanding = 0,
  filling = true;
let pool = [],
  maxRenderUs = 0,
  shared = null,
  commandTime = 0,
  commandTrack = -1;
// Half-second target rounded up to 512 ms; refill at 427 ms for stall headroom.
// Keep MusicBuffer.h and music-output.js aligned.
const target = 24,
  low = 20,
  capacity = 48;
let chain = Promise.resolve();
// Preview pause belongs to the consumer: retaining prepared PCM also retains
// the exact decoder/fade state for resume. The producer may finish its prefill.
let previewPaused = false,
  positionRequest = 0,
  generationPosition = 0;
const positionReplies = new Map();
function pausePreview(paused) {
  previewPaused = paused;
  port.postMessage({ paused });
}
function consumedPosition() {
  const request = ++positionRequest;
  return new Promise((resolve, reject) => {
    // Suspended or unavailable audio must not hold the application's command
    // credit forever. Reject this reset instead of guessing a stale cursor.
    const timeout = setTimeout(() => {
      positionReplies.delete(request);
      reject(Error('Audio output did not acknowledge preview reset'));
    }, 1000);
    positionReplies.set(request, (position) => {
      clearTimeout(timeout);
      resolve(position);
    });
    port.postMessage({ positionRequest: request });
  });
}
function snapshot() {
  return JSON.parse(runtime.UTF8ToString(runtime._audio_snapshot()));
}
function bytes(buffer, fn) {
  const data = new Uint8Array(buffer),
    p = runtime._malloc(data.length);
  if (!p) throw Error('Not enough memory for music');
  try {
    runtime.HEAPU8.set(data, p);
    return fn(p, data.length);
  } finally {
    runtime._free(p);
  }
}
function reset() {
  ++generation;
  filling = true;
  generationPosition = snapshot().position;
  // Credits are returned for stale blocks too; never mint new credits on a reset.
  if (shared) Atomics.store(shared.control, Shared.Generation, generation);
  else port.postMessage({ reset: true, generation });
}
function pump() {
  if (!runtime || !port || hidden || !enabled) return;
  if (shared) {
    if (Atomics.load(shared.control, Shared.Acknowledged) !== generation) return;
    outstanding =
      (Atomics.load(shared.control, Shared.Write) - Atomics.load(shared.control, Shared.Read)) >>>
      0;
    if (Atomics.load(shared.control, Shared.NeedsPrefill)) filling = true;
  }
  if (outstanding <= low) filling = true;
  if (!filling) return;
  // Limit each event-loop turn so commands can interrupt a refill.
  for (let n = 0; n < 4 && outstanding < target && (shared || pool.length); n++) {
    const start = performance.now(),
      state = snapshot(),
      p = runtime._audio_render();
    state.commandTime =
      state.track === commandTrack || (state.mode === 2 && state.next === commandTrack)
        ? commandTime
        : 0;
    maxRenderUs = Math.max(maxRenderUs, Math.round((performance.now() - start) * 1000));
    if (shared) {
      const write = Atomics.load(shared.control, Shared.Write),
        slot = (write >>> 0) % 64;
      shared.pcm.set(runtime.HEAP16.subarray(p / 2, p / 2 + 2048), slot * 2048);
      // Metadata slots: track, next, pending, mode, preview, playing, audition,
      // failed, position, duration, three weights, render time, command time.
      const values = [
        state.track,
        state.next,
        state.pending,
        state.mode,
        state.preview ? 1 : 0,
        state.playing ? 1 : 0,
        state.audition ? 1 : 0,
        state.failed ? 1 : 0,
        state.position,
        state.duration,
        ...state.weights,
        maxRenderUs,
        state.commandTime,
      ];
      shared.metadata.set(values, slot * 16);
      Atomics.store(shared.control, Shared.Write, (write + 1) | 0);
    } else {
      const buffer = pool.pop(),
        pcm = new Int16Array(buffer);
      pcm.set(runtime.HEAP16.subarray(p / 2, p / 2 + 2048));
      port.postMessage({ pcm: buffer, generation, state, maxRenderUs }, [buffer]);
    }
    outstanding++;
  }
  if (outstanding < target && (shared || pool.length)) setTimeout(pump, 0);
  else filling = false;
}
async function message(m) {
  if (m.type === 'initialize') {
    runtime = await createMusicRuntime();
    port = m.port;
    if (m.shared) {
      shared = {
        control: new Int32Array(m.shared.control),
        pcm: new Int16Array(m.shared.pcm),
        metadata: new Float64Array(m.shared.metadata),
      };
      setInterval(pump, 5);
    }
    pool = Array.from({ length: capacity }, () => new ArrayBuffer(4096));
    port.onmessage = (e) => {
      if (e.data.positionRequest) {
        const resolve = positionReplies.get(e.data.positionRequest);
        positionReplies.delete(e.data.positionRequest);
        resolve?.(
          e.data.generation === generation && e.data.position !== null
            ? e.data.position
            : generationPosition,
        );
      }
      if (e.data.prefill) {
        filling = true;
        pump();
      }
      if (e.data.recycle) {
        pool.push(e.data.recycle);
        outstanding--;
        pump();
      }
    };
    port.start();
    postMessage({ ready: true });
    return;
  }
  if (!runtime) return;
  if (m.type === 'load') {
    if (bytes(m.bytes, (p, n) => runtime._audio_load(m.index, p, n)) < 0)
      throw Error('Invalid music track');
    reset();
  } else if (m.type === 'replace' || m.type === 'preview') {
    m.tracks.forEach((b, i) => bytes(b, (p, n) => runtime._audio_stage(i, p, n)));
    if (!runtime._audio_replace(m.type === 'preview')) throw Error('Invalid music set');
    pausePreview(m.type === 'preview');
    if (m.type === 'preview') runtime._audio_control(Control.Play, 1);
    reset();
  } else if (m.type === 'select') {
    runtime._audio_select(m.index, m.value, enabled);
    commandTime = enabled ? m.sentAt || 0 : 0;
    commandTrack = m.index;
  } else if (m.type === 'stop') runtime._audio_stop();
  else if (m.type === 'closePreview') {
    runtime._audio_close_preview();
    pausePreview(false);
    reset();
  } else if (m.type === 'control') {
    if (m.index === Control.Play) {
      pausePreview(!m.value);
    } else {
      try {
        if (m.index === Control.Reset) {
          // Reset changes preview controls, not the timeline. Freeze consumption
          // before asking for its position, then discard only the old preparation.
          port.postMessage({ paused: true });
          runtime._audio_control(Control.Seek, await consumedPosition());
        }
        runtime._audio_control(m.index, m.value);
        if (m.index === Control.Seek || m.index === Control.Reset) reset();
      } finally {
        if (m.index === Control.Reset) port.postMessage({ paused: previewPaused });
      }
    }
  } else if (m.type === 'enable') {
    if (!enabled) {
      const s = snapshot();
      if (s.track >= 0) runtime._audio_select(s.track, 0, 1);
    }
    enabled = true;
  } else if (m.type === 'hidden') {
    hidden = !!m.value;
    if (!hidden) filling = true;
  }
  pump();
}
onmessage = (e) => {
  chain = chain
    .then(() => message(e.data))
    .catch((error) => postMessage({ error: String(error.message || error) }))
    .finally(() => {
      if (e.data.type !== 'initialize') postMessage({ ack: true });
    });
};
