// SPDX-License-Identifier: GPL-3.0-or-later
// Invoke directly from a user gesture, while browser activation is still valid.
async function glob2ActivateAudio(context, reportError) {
  if (!context || context.state === 'closed' || context.state === 'running') return;
  try {
    await context.resume();
  } catch (error) {
    // SDL can close the context while a gesture's resume promise is pending.
    // A later gesture may retry other failures; never leave a rejected promise
    // unhandled or claim that audio started when the browser refused it.
    if (context.state !== 'closed') reportError(error);
  }
}
// Firefox rejects a pending SDL resume after SDL_Quit closes the AudioContext.
// The rejection is delivered after the synchronous shutdown has completed, so
// suppress only that exact, otherwise unobserved shutdown race.
glob2ActivateAudio.ignoreClosedRejection = function (event, exited) {
  const reason = event?.reason;
  if (
    !exited ||
    reason?.name !== 'InvalidStateError' ||
    !/closed before resume completed/i.test(reason?.message || '')
  )
    return false;
  event.preventDefault();
  return true;
};
if (typeof module !== 'undefined' && module.exports) module.exports = glob2ActivateAudio;
if (typeof globalThis !== 'undefined') globalThis.glob2ActivateAudio = glob2ActivateAudio;

// The application realm sends control messages; the UI owns only device lifecycle.
// Worker -> worklet PCM never traverses the UI or the application worker.
if (
  typeof Module !== 'undefined' &&
  (typeof ENVIRONMENT_IS_PTHREAD === 'undefined' || !ENVIRONMENT_IS_PTHREAD)
) {
  Module.glob2Music = {
    context: null,
    node: null,
    worker: null,
    ready: false,
    initializing: null,
    closed: false,
    pending: new Map(),
    status: {},
    gain: 1,
    clock: 0,
    inFlight: false,
    send(m) {
      if (m.type === 'destroy') {
        this.destroy();
        return;
      }
      if (this.closed) return;
      m.sentAt = this.clock + performance.now() * 1000;
      const key =
        m.type === 'load'
          ? 'load:' + m.index
          : m.type === 'control'
            ? 'control:' + m.index
            : m.type;
      if (m.type === 'preview' || m.type === 'closePreview') {
        for (const k of this.pending.keys()) if (k.startsWith('control:')) this.pending.delete(k);
      }
      this.pending.delete(key);
      this.pending.set(key, m);
      this.flush();
    },
    flush() {
      if (!this.ready || this.inFlight || !this.pending.size) return;
      const [key, m] = this.pending.entries().next().value;
      this.pending.delete(key);
      this.inFlight = true;
      this.worker.postMessage(m, this.transfers(m));
    },
    transfers(m) {
      return m.tracks || (m.bytes ? [m.bytes] : []);
    },
    volume(value, mute, now) {
      this.gain = mute ? 0 : value === 255 ? 1 : value / 256;
      this.clock = now - performance.now() * 1000;
      this.node?.port.postMessage({ gain: this.gain });
      if (!mute) this.initialize();
    },
    async initialize() {
      if (this.initializing || this.closed) return this.initializing;
      this.initializing = (async () => {
        const context = (this.context = new AudioContext({ latencyHint: 'playback' }));
        // Resume in the initiating gesture, before loading the worklet yields.
        const activation = document.hidden
          ? Promise.resolve()
          : glob2ActivateAudio(context, (error) => Module.printErr?.('Audio activation: ' + error));
        const base = document.baseURI;
        await context.audioWorklet.addModule(new URL('music-output.js', base));
        if (this.closed) return;
        const node = (this.node = new AudioWorkletNode(context, 'glob2-music', {
          numberOfInputs: 0,
          outputChannelCount: [2],
        }));
        const worker = (this.worker = new Worker(new URL('music-worker.js', base)));
        const channel = new MessageChannel();
        // WebKit can delay MessagePort delivery during a UI long task. Shared
        // memory is an optional transport, never a requirement for serial play.
        const shared =
          globalThis.crossOriginIsolated && typeof SharedArrayBuffer === 'function'
            ? {
                control: new SharedArrayBuffer(8 * 4),
                pcm: new SharedArrayBuffer(64 * 4096),
                metadata: new SharedArrayBuffer(64 * 16 * 8),
              }
            : null;
        this.transport = shared ? 'shared' : 'messages';
        node.port.postMessage(
          { audioPort: channel.port1, shared, gain: this.gain, hidden: document.hidden },
          [channel.port1],
        );
        // Fixed capture credits. A busy UI can drop capture, never stall playback.
        for (let i = 0; i < 8; i++) {
          const capture = new ArrayBuffer(4096);
          node.port.postMessage({ capture }, [capture]);
        }
        let audioClock = this.clock + performance.now() * 1000 - context.currentTime * 1000000;
        node.port.postMessage({ clock: audioClock });
        node.port.onmessage = (e) => {
          if (this.closed) return;
          const m = e.data;
          if (m.status) {
            const delay =
              m.status.commandTime && m.status.audibleAt
                ? Math.max(0, m.status.audibleAt - m.status.commandTime)
                : 0;
            const maximum = Math.max(this.status.maxCommandLatencyUs || 0, delay);
            this.status = { ...m.status, maxCommandLatencyUs: maximum };
            node.port.postMessage({ recording: !!Module._glob2_audio_recording_active?.() });
          }
          if (m.captured) {
            try {
              const bytes = new Uint8Array(m.captured),
                p = Module._malloc(bytes.length);
              if (p) {
                try {
                  HEAPU8.set(bytes, p);
                  Module._glob2_audio_capture(p, bytes.length / 2, m.time);
                } finally {
                  Module._free(p);
                }
              }
            } catch (error) {
              if (!this.captureFailed) Module.printErr?.('Audio capture: ' + error);
              this.captureFailed = true;
            } finally {
              node.port.postMessage({ capture: m.captured }, [m.captured]);
            }
          }
        };
        const visibility = () => {
          node.port.postMessage({ hidden: document.hidden });
          this.send({ type: 'hidden', value: document.hidden });
          if (document.hidden)
            context.suspend().catch((error) => Module.printErr?.('Audio pause: ' + error));
          else {
            audioClock = this.clock + performance.now() * 1000 - context.currentTime * 1000000;
            node.port.postMessage({ clock: audioClock });
            glob2ActivateAudio(context, (error) => Module.printErr?.('Audio activation: ' + error));
          }
        };
        this.visibility = visibility;
        document.addEventListener('visibilitychange', visibility);
        worker.onmessage = (e) => {
          if (this.closed) return;
          if (e.data.ready) {
            this.ready = true;
            this.flush();
            this.send({ type: 'hidden', value: document.hidden });
            this.send({ type: 'enable' });
          } else if (e.data.ack) {
            this.inFlight = false;
            this.flush();
          } else if (e.data.error) {
            this.status = { ...this.status, failed: true };
            Module.printErr?.('Music: ' + e.data.error);
          }
        };
        worker.onerror = (e) => {
          this.status = { ...this.status, failed: true };
          Module.printErr?.('Music worker: ' + e.message);
        };
        worker.postMessage({ type: 'initialize', port: channel.port2, shared }, [channel.port2]);
        node.connect(context.destination);
        if (document.hidden) await context.suspend();
        else await activation;
      })().catch((error) => {
        this.status = { failed: true };
        Module.printErr?.('Music unavailable: ' + error);
        this.worker?.terminate();
        this.context?.close().catch(() => {});
        this.context = null;
      });
      return this.initializing;
    },
    destroy() {
      this.closed = true;
      this.ready = false;
      this.pending.clear();
      if (this.visibility) document.removeEventListener('visibilitychange', this.visibility);
      this.node?.disconnect();
      this.node?.port.close();
      this.worker?.terminate();
      this.context?.close().catch(() => {});
      this.context = null;
    },
  };
}
