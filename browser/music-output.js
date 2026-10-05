// SPDX-License-Identifier: GPL-3.0-or-later
// Bounded, recycled PCM; no decoder, filesystem, game calls or blocking waits.
// Shared ring protocol (keep aligned in worker and worklet): the worker publishes
// PCM/metadata before Write; the consumer alone advances Read. A generation change
// is acknowledged only after old slots are discarded, before new slots are reused.
const Shared = { Read: 0, Write: 1, Generation: 2, Acknowledged: 3, NeedsPrefill: 4 };
// Matches MusicTypes/MusicBuffer and the worker's fixed PCM credits.
const Rate = 48000,
  BlockFrames = 1024,
  BlockSamples = BlockFrames * 2;
const TargetBlocks = 36,
  CapacityBlocks = 48,
  StorageBlocks = 64;
const RampFrames = 240; // five milliseconds at the source rate

class Glob2MusicOutput extends AudioWorkletProcessor {
  constructor() {
    super();
    this.blocks = new Array(CapacityBlocks);
    this.read = 0;
    this.write = 0;
    this.count = 0;
    this.offset = 0;
    this.fraction = 0;
    this.generation = 0;
    this.buffering = true;
    this.hidden = false;
    this.paused = false;
    this.gain = 1;
    this.fade = 0;
    this.last = [0, 0];
    this.starvationFrames = 0;
    this.underruns = 0;
    this.consumedFrames = 0;
    this.reportAt = 0;
    this.state = {};
    this.sharedWeights = [1, 0, 0];
    this.maxRenderUs = 0;
    this.started = false;
    this.commandTime = 0;
    this.audibleAt = 0;
    this.shared = null;
    this.recording = false;
    this.captureFrames = 0;
    this.droppedCaptureFrames = 0;
    this.clock = 0;
    this.capturePool = [];
    this.capture = null;
    this.captureAt = 0;
    this.captureStart = 0;
    this.port.onmessage = (e) => {
      const m = e.data;
      if (m.shared)
        this.shared = {
          control: new Int32Array(m.shared.control),
          pcm: new Int16Array(m.shared.pcm),
          metadata: new Float64Array(m.shared.metadata),
        };
      if (m.audioPort) {
        this.audio = m.audioPort;
        this.audio.onmessage = (e) => this.receive(e.data);
        this.audio.start();
      }
      if (m.gain !== undefined) this.gain = m.gain;
      if (m.hidden !== undefined) {
        this.hidden = m.hidden;
        if (this.hidden) {
          this.buffering = true;
          this.started = false;
          this.fade = 0;
          if (this.capture) {
            this.capturePool.push(this.capture);
            this.capture = null;
            this.captureAt = 0;
          }
        }
      }
      if (m.capture) this.capturePool.push(new Int16Array(m.capture));
      if (m.clock !== undefined || m.recording !== undefined) {
        if (m.clock !== undefined) this.clock = m.clock;
        if (m.recording !== undefined && m.recording !== this.recording) {
          this.recording = m.recording;
          if (this.capture) {
            this.capturePool.push(this.capture);
            this.capture = null;
            this.captureAt = 0;
          }
        }
      }
    };
  }
  recycle() {
    const block = this.blocks[this.read];
    this.blocks[this.read] = null;
    this.read = (this.read + 1) % CapacityBlocks;
    --this.count;
    this.audio.postMessage({ recycle: block.pcm.buffer }, [block.pcm.buffer]);
  }
  receive(m) {
    if (m.paused !== undefined) {
      this.paused = m.paused;
      // Pause already outputs silence. A later seek/reset must not resurrect
      // the last audible sample as its generation-change fade-out tail.
      if (this.paused) this.last[0] = this.last[1] = 0;
    }
    if (m.positionRequest)
      this.audio.postMessage({
        positionRequest: m.positionRequest,
        position: this.count ? this.position() : null,
        generation: this.generation,
      });
    if (m.reset) {
      while (this.count) this.recycle();
      this.generation = m.generation;
      this.offset = this.fraction = 0;
      this.buffering = true;
      this.started = false;
      this.fade = RampFrames;
    } else if (m.pcm) {
      if (m.generation !== this.generation || this.count === CapacityBlocks) {
        this.audio.postMessage({ recycle: m.pcm }, [m.pcm]);
        return;
      }
      // Views are constructed in message handling, never in process().
      m.pcm = new Int16Array(m.pcm);
      this.blocks[this.write] = m;
      this.write = (this.write + 1) % CapacityBlocks;
      ++this.count;
    }
  }
  // The unread block describes its starting cursor. Include the consumed part
  // of that block, rather than displaying the producer's look-ahead position.
  position() {
    if (!this.count) return this.state.position || 0;
    let position, duration, playing;
    if (this.shared) {
      const slot = (Atomics.load(this.shared.control, Shared.Read) >>> 0) % StorageBlocks,
        j = slot * 16;
      position = this.shared.metadata[j + 8];
      duration = this.shared.metadata[j + 9];
      playing = !!this.shared.metadata[j + 5];
    } else {
      const state = this.blocks[this.read].state;
      position = state.position || 0;
      duration = state.duration;
      playing = state.playing;
    }
    if (playing) position += (this.offset + this.fraction) / Rate;
    return duration ? position % duration : position;
  }
  captureSample(left, right, time) {
    if (!this.recording) return;
    if (!this.capture && this.capturePool.length) {
      this.capture = this.capturePool.pop();
      this.captureStart = time;
    }
    if (!this.capture) {
      this.droppedCaptureFrames++;
      return;
    }
    this.captureFrames++;
    this.capture[this.captureAt++] = Math.max(-32768, Math.min(32767, Math.round(left * 32768)));
    this.capture[this.captureAt++] = Math.max(-32768, Math.min(32767, Math.round(right * 32768)));
    if (this.captureAt === BlockSamples) {
      const buffer = this.capture.buffer;
      this.port.postMessage({ captured: buffer, time: this.clock + this.captureStart * 1000000 }, [
        buffer,
      ]);
      this.capture = null;
      this.captureAt = 0;
    }
  }
  process(inputs, outputs) {
    const out = outputs[0];
    if (!out?.[0]) return true;
    if (this.shared) {
      const control = this.shared.control,
        gen = Atomics.load(control, Shared.Generation);
      if (gen !== this.generation) {
        Atomics.store(control, Shared.Read, Atomics.load(control, Shared.Write));
        this.generation = gen;
        this.offset = this.fraction = 0;
        this.buffering = true;
        this.started = false;
        this.fade = RampFrames;
        Atomics.store(control, Shared.Acknowledged, gen);
      }
      this.count = (Atomics.load(control, Shared.Write) - Atomics.load(control, Shared.Read)) >>> 0;
    }
    if (this.hidden) return true;
    if (this.paused) {
      // Paused preview audio is silence, but recording's output clock continues.
      const ratio = Rate / sampleRate;
      for (let i = 0; i < out[0].length; ++i) {
        this.captureFraction = (this.captureFraction || 0) + ratio;
        while (this.captureFraction >= 1) {
          this.captureFraction--;
          this.captureSample(0, 0, currentTime + i / sampleRate);
        }
      }
      this.report(out[0].length);
      return true;
    }
    if (this.buffering && !this.fade && this.count >= TargetBlocks) {
      this.buffering = false;
      this.fade = RampFrames;
      this.started = true;
    }
    if (this.shared)
      Atomics.store(this.shared.control, Shared.NeedsPrefill, this.buffering ? 1 : 0);
    const ratio = Rate / sampleRate;
    for (let i = 0; i < out[0].length; ++i) {
      if (!this.buffering && !this.count) {
        this.buffering = true;
        this.fade = RampFrames;
        this.underruns++;
        if (this.shared) Atomics.store(this.shared.control, Shared.NeedsPrefill, 1);
        else this.audio.postMessage({ prefill: true });
      }
      let left = 0,
        right = 0;
      if (!this.buffering) {
        const block = this.shared ? null : this.blocks[this.read];
        const slot = this.shared
          ? (Atomics.load(this.shared.control, Shared.Read) >>> 0) % StorageBlocks
          : 0;
        const pcm = this.shared ? this.shared.pcm : block.pcm;
        const next = this.shared
          ? pcm
          : this.offset + 1 < BlockFrames
            ? block.pcm
            : this.count > 1
              ? this.blocks[(this.read + 1) % CapacityBlocks].pcm
              : block.pcm;
        const base = this.shared ? slot * BlockSamples : 0;
        const at =
          this.offset + 1 < BlockFrames
            ? base + (this.offset + 1) * 2
            : this.count > 1
              ? this.shared
                ? ((slot + 1) % StorageBlocks) * BlockSamples
                : 0
              : base + this.offset * 2;
        const here = base + this.offset * 2;
        const fade = this.fade ? (RampFrames - this.fade) / RampFrames : 1;
        left = ((pcm[here] + (next[at] - pcm[here]) * this.fraction) / 32768) * fade;
        right = ((pcm[here + 1] + (next[at + 1] - pcm[here + 1]) * this.fraction) / 32768) * fade;
        this.last[0] = left;
        this.last[1] = right;
        this.fraction += ratio;
        while (this.fraction >= 1 && this.count) {
          this.fraction--;
          this.consumedFrames++;
          this.captureSample(left * this.gain, right * this.gain, currentTime + i / sampleRate);
          if (++this.offset === BlockFrames) {
            if (this.shared) {
              const m = this.shared.metadata,
                j = slot * 16,
                s = this.state;
              s.track = m[j];
              s.next = m[j + 1];
              s.pending = m[j + 2];
              s.mode = m[j + 3];
              s.preview = !!m[j + 4];
              s.playing = !!m[j + 5];
              s.audition = !!m[j + 6];
              s.failed = !!m[j + 7];
              s.position = m[j + 8];
              s.duration = m[j + 9];
              this.maxRenderUs = m[j + 13];
              s.commandTime = m[j + 14];
              this.sharedWeights[0] = m[j + 10];
              this.sharedWeights[1] = m[j + 11];
              this.sharedWeights[2] = m[j + 12];
              s.weights = this.sharedWeights;
              Atomics.add(this.shared.control, Shared.Read, 1);
              --this.count;
            } else {
              this.state = block.state;
              this.maxRenderUs = block.maxRenderUs;
              this.recycle();
            }
            if (this.state.commandTime && this.state.commandTime !== this.commandTime) {
              this.commandTime = this.state.commandTime;
              this.audibleAt = this.clock + (currentTime + i / sampleRate) * 1000000;
            }
            if (this.state.playing && this.state.duration)
              this.state.position =
                (this.state.position + BlockFrames / Rate) % this.state.duration;
            this.offset = 0;
          }
        }
      } else {
        left = (this.last[0] * this.fade) / RampFrames;
        right = (this.last[1] * this.fade) / RampFrames;
        if (this.started) this.starvationFrames += ratio;
        this.fraction += ratio;
        while (this.fraction >= 1) {
          this.fraction--;
          this.captureSample(left * this.gain, right * this.gain, currentTime + i / sampleRate);
        }
      }
      if (this.fade) --this.fade;
      out[0][i] = left * this.gain;
      if (out[1]) out[1][i] = right * this.gain;
    }
    this.report(out[0].length);
    return true;
  }
  report(frames) {
    if ((this.reportAt += frames) >= sampleRate / 10) {
      this.reportAt = 0;
      let state = this.state;
      if (this.count) {
        if (this.shared) {
          const j = ((Atomics.load(this.shared.control, Shared.Read) >>> 0) % StorageBlocks) * 16,
            m = this.shared.metadata;
          state = {
            track: m[j],
            next: m[j + 1],
            pending: m[j + 2],
            mode: m[j + 3],
            preview: !!m[j + 4],
            playing: !!m[j + 5],
            audition: !!m[j + 6],
            failed: !!m[j + 7],
            duration: m[j + 9],
            weights: [m[j + 10], m[j + 11], m[j + 12]],
          };
        } else state = this.blocks[this.read].state;
      }
      this.port.postMessage({
        status: {
          ...state,
          position: this.position(),
          playing: this.paused ? false : state.playing,
          commandTime: this.commandTime,
          audibleAt: this.audibleAt,
          queuedFrames: this.count * BlockFrames - this.offset,
          starvationFrames: Math.floor(this.starvationFrames),
          underruns: this.underruns,
          consumedFrames: this.consumedFrames,
          maxRenderUs: this.maxRenderUs,
          recording: this.recording,
          captureFrames: this.captureFrames,
          droppedCaptureFrames: this.droppedCaptureFrames,
          captureBuffers: this.capturePool.length,
          captureAt: this.captureAt,
        },
      });
    }
  }
}
registerProcessor('glob2-music', Glob2MusicOutput);
