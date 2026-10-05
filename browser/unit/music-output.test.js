const { test } = require('node:test');
const assert = require('node:assert/strict');
const vm = require('node:vm');
const fs = require('node:fs');
function setup(rate = 48000) {
  let Processor;
  const messages = [],
    recycled = [];
  const context = {
    sampleRate: rate,
    currentTime: 0,
    AudioWorkletProcessor: class {
      constructor() {
        this.port = { postMessage: (m) => messages.push(m) };
      }
    },
    registerProcessor: (_, p) => (Processor = p),
  };
  vm.createContext(context);
  vm.runInContext(fs.readFileSync(require.resolve('../music-output.js'), 'utf8'), context);
  const processor = new Processor();
  processor.audio = { postMessage: (m) => recycled.push(m.recycle) };
  function fill(count = 36, value = 12000, generation = processor.generation) {
    for (let i = 0; i < count; i++) {
      const pcm = new Int16Array(2048);
      pcm.fill(value);
      processor.receive({
        pcm: pcm.buffer,
        generation,
        state: { position: (i * 1024) / 48000 },
        maxRenderUs: 100,
      });
    }
  }
  function render(frames = 128) {
    const out = [new Float32Array(frames), new Float32Array(frames)];
    processor.process([], [out]);
    context.currentTime += frames / rate;
    return out;
  }
  return { processor, fill, render, messages, recycled };
}
test('400ms producer stall preserves prepared audio at both device rates', () => {
  for (const rate of [44100, 48000]) {
    const { processor: p, fill, render } = setup(rate);
    fill();
    for (let n = 0; n < Math.ceil((rate * 0.4) / 128); n++) render();
    assert.equal(p.underruns, 0);
    assert.equal(p.starvationFrames, 0);
    assert.ok(p.count > 0);
  }
});
test('undersupply waits for target and starvation recovers once', () => {
  const { processor: p, fill, render } = setup();
  fill(24);
  render();
  assert.equal(p.consumedFrames, 0);
  fill(12);
  for (let n = 0; n < 310; n++) render();
  assert.equal(p.underruns, 1);
  assert.ok(render()[0].every((n) => n === 0));
  fill();
  render();
  assert.equal(p.underruns, 1);
  assert.equal(p.buffering, false);
});
test('generation resets recycle credits and reject stale packets', () => {
  const { processor: p, fill, render, recycled } = setup();
  fill();
  render();
  p.receive({ reset: true, generation: 1 });
  assert.equal(recycled.length, 36);
  fill(1, 1000, 0);
  assert.equal(recycled.length, 37);
  assert.equal(p.count, 0);
  fill(36, -12000, 1);
  render(1024);
  render(1024);
  assert.ok(render()[0].every((n) => n < 0));
});
test('mute is immediate and visibility freezes consumption', () => {
  const { processor: p, fill, render } = setup();
  fill();
  render();
  p.port.onmessage({ data: { gain: 0 } });
  assert.ok(render()[0].every((n) => n === 0));
  const before = p.consumedFrames;
  p.port.onmessage({ data: { hidden: true } });
  render();
  assert.equal(p.consumedFrames, before);
  fill(36 - p.count);
  p.port.onmessage({ data: { hidden: false, gain: 1 } });
  render();
  assert.ok(p.consumedFrames > before);
});
test('queue is bounded and resampling does not drift over block boundaries', () => {
  for (const rate of [44100, 48000]) {
    const { processor: p, fill, render } = setup(rate);
    fill(49);
    assert.equal(p.count, 48);
    for (let n = 0; n < 1000; n++) {
      if (p.count < 24) fill(36 - p.count);
      render();
    }
    assert.ok(Math.abs(p.consumedFrames - (128000 * 48000) / rate) < 2);
    assert.equal(p.underruns, 0);
  }
});

test('shared transport needs no message delivery to keep supplying audio', () => {
  const { processor: p, render } = setup();
  const shared = {
    control: new SharedArrayBuffer(32),
    pcm: new SharedArrayBuffer(64 * 4096),
    metadata: new SharedArrayBuffer(64 * 16 * 8),
  };
  const control = new Int32Array(shared.control),
    pcm = new Int16Array(shared.pcm);
  p.port.onmessage({ data: { shared } });
  pcm.fill(10000);
  Atomics.store(control, 1, 36);
  for (let n = 0; n < 2000; n++) {
    const read = Atomics.load(control, 0),
      write = Atomics.load(control, 1);
    if (write - read < 24) Atomics.store(control, 1, read + 36);
    render();
  }
  assert.equal(p.underruns, 0);
  assert.equal(p.consumedFrames, 256000);
});
test('capture is bounded and uses the consumed clock after gain', () => {
  const { processor: p, fill, render, messages } = setup();
  fill();
  p.port.onmessage({
    data: { recording: true, clock: 1000000, gain: 0.5, capture: new ArrayBuffer(4096) },
  });
  render(1024);
  render(1024);
  const captured = messages.filter((m) => m.captured);
  assert.equal(captured.length, 1);
  assert.equal(captured[0].time, 1000000);
  assert.equal(new Int16Array(captured[0].captured)[2047], 6000);
});

test('preview pause retains the exact next sample and cursor at both rates and transports', () => {
  for (const rate of [44100, 48000])
    for (const shared of [false, true]) {
      function prepared() {
        const harness = setup(rate),
          p = harness.processor;
        if (shared) {
          const buffers = {
            control: new SharedArrayBuffer(32),
            pcm: new SharedArrayBuffer(64 * 4096),
            metadata: new SharedArrayBuffer(64 * 16 * 8),
          };
          const pcm = new Int16Array(buffers.pcm),
            metadata = new Float64Array(buffers.metadata);
          for (let i = 0; i < pcm.length; i++) pcm[i] = (i % 24000) - 12000;
          for (let i = 0; i < 36; i++) {
            metadata[i * 16 + 4] = metadata[i * 16 + 5] = 1;
            metadata[i * 16 + 8] = (i * 1024) / 48000;
            metadata[i * 16 + 9] = 60;
          }
          p.port.onmessage({ data: { shared: buffers } });
          Atomics.store(new Int32Array(buffers.control), 1, 36);
        } else
          for (let i = 0; i < 36; i++) {
            const pcm = new Int16Array(2048);
            for (let j = 0; j < pcm.length; j++) pcm[j] = ((i * 2048 + j) % 24000) - 12000;
            p.receive({
              pcm: pcm.buffer,
              generation: 0,
              state: { preview: true, playing: true, position: (i * 1024) / 48000, duration: 60 },
              maxRenderUs: 0,
            });
          }
        harness.render(1536);
        return harness;
      }
      const actual = prepared(),
        reference = prepared(),
        p = actual.processor;
      const position = p.position(),
        consumed = p.consumedFrames;
      p.receive({ paused: true });
      for (let i = 0; i < 100; i++) assert.ok(actual.render()[0].every((sample) => sample === 0));
      assert.equal(p.position(), position);
      assert.equal(p.consumedFrames, consumed);
      const status = actual.messages.filter((m) => m.status).at(-1).status;
      assert.equal(status.preview, true);
      assert.equal(status.playing, false);
      assert.equal(status.position, position);
      p.receive({ paused: false });
      assert.deepEqual(actual.render(2048), reference.render(2048));
      assert.equal(p.underruns, 0);
      assert.equal(p.starvationFrames, 0);
    }
});

test('visibility rebuffering is intentional silence, not producer starvation', () => {
  const { processor: p, fill, render } = setup();
  fill();
  render(1536);
  p.port.onmessage({ data: { hidden: true } });
  render();
  p.port.onmessage({ data: { hidden: false } });
  render(1024);
  assert.equal(p.starvationFrames, 0);
  assert.equal(p.underruns, 0);
  fill(36 - p.count);
  render();
  assert.equal(p.buffering, false);
});

test('paused generation changes never replay the old audible tail', () => {
  for (const shared of [false, true]) {
    const { processor: p, fill, render } = setup();
    let buffers, control, pcm;
    if (shared) {
      buffers = {
        control: new SharedArrayBuffer(32),
        pcm: new SharedArrayBuffer(64 * 4096),
        metadata: new SharedArrayBuffer(64 * 16 * 8),
      };
      control = new Int32Array(buffers.control);
      pcm = new Int16Array(buffers.pcm);
      p.port.onmessage({ data: { shared: buffers } });
      pcm.fill(12000);
      Atomics.store(control, 1, 36);
    } else fill();
    render(1536);
    p.receive({ paused: true });
    render(1024);
    for (const generation of [1, 2]) {
      const value = generation === 1 ? -12000 : 12000;
      if (shared) Atomics.store(control, 2, generation);
      else p.receive({ reset: true, generation });
      render(); // acknowledge/discard before replacing shared slots
      if (shared) {
        pcm.fill(value);
        Atomics.store(control, 1, Atomics.load(control, 0) + 36);
      } else fill(36, value, generation);
      p.receive({ paused: false });
      // Startup may remain silent until prefill/fade completes; samples with the old sign
      // must never appear, even when reset is repeated while paused.
      for (let i = 0; i < 4; i++) assert.ok(render()[0].every((sample) => sample * value >= 0));
      assert.ok(render()[0].every((sample) => sample * value > 0));
      p.receive({ paused: true });
      render(1024);
    }
  }
});
