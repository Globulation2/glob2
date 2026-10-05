const { test } = require('node:test');
const assert = require('node:assert/strict');
const activate = require('../audio');

test('audio activation ignores missing, running and closed contexts', async () => {
  await activate(undefined, assert.fail);
  for (const state of ['running', 'closed'])
    await activate({ state, resume: assert.fail }, assert.fail);
});

test('closing audio during a pending resume does not reject the gesture handler', async () => {
  let reject;
  const context = {
    state: 'suspended',
    resume: () =>
      new Promise((_, fail) => {
        reject = fail;
      }),
  };
  const pending = activate(context, assert.fail);
  context.state = 'closed';
  reject(new Error('Closed before resume completed'));
  await pending;
});

test('activation failures are reported and the next gesture can retry', async () => {
  const failure = new Error('Audio device unavailable');
  const errors = [];
  const context = {
    state: 'suspended',
    resume: async () => {
      throw failure;
    },
  };
  await activate(context, (error) => errors.push(error));
  assert.deepEqual(errors, [failure]);
  assert.equal(context.state, 'suspended');
  context.resume = () => {
    context.state = 'running';
    return Promise.resolve();
  };
  const pending = activate(context, assert.fail);
  // resume must run synchronously in the gesture, before the first await.
  assert.equal(context.state, 'running');
  await pending;
});

test('only Firefox closed-resume rejection is suppressed after game exit', () => {
  let prevented = 0;
  const event = {
    reason: { name: 'InvalidStateError', message: 'Closed before resume completed' },
    preventDefault: () => ++prevented,
  };
  assert.equal(activate.ignoreClosedRejection(event, false), false);
  assert.equal(
    activate.ignoreClosedRejection(
      {
        ...event,
        reason: { name: 'InvalidStateError', message: 'Another audio error' },
      },
      true,
    ),
    false,
  );
  assert.equal(prevented, 0);
  assert.equal(activate.ignoreClosedRejection(event, true), true);
  assert.equal(prevented, 1);
});

const vm = require('node:vm');
const fs = require('node:fs');
function musicHost() {
  const sent = [];
  const context = { Module: {}, performance: { now: () => 0 }, globalThis: {}, console };
  vm.createContext(context);
  vm.runInContext(fs.readFileSync(require.resolve('../audio'), 'utf8'), context);
  const host = context.Module.glob2Music;
  host.ready = true;
  host.worker = { postMessage: (m) => sent.push(m) };
  return { host, sent };
}
test('music controls coalesce behind one in-flight worker command', () => {
  const { host, sent } = musicHost();
  host.send({ type: 'load', index: 0, bytes: new ArrayBuffer(1) });
  for (let n = 0; n < 1000; n++) host.send({ type: 'select', index: n % 3 });
  assert.equal(sent.length, 1);
  assert.equal(host.pending.size, 1);
  host.inFlight = false;
  host.flush();
  assert.equal(sent.length, 2);
  assert.equal(sent[1].index, 999 % 3);
});
test('replacing a preview discards controls for its previous session', () => {
  const { host, sent } = musicHost();
  host.send({ type: 'load', index: 0, bytes: new ArrayBuffer(1) });
  host.send({ type: 'control', index: 2, value: 15 });
  host.send({ type: 'preview', tracks: [] });
  assert.equal(host.pending.size, 1);
  host.inFlight = false;
  host.flush();
  assert.equal(sent[1].type, 'preview');
});

test('capture bridge frees memory and returns credits even when capture fails', async () => {
  const sent = [],
    freed = [],
    errors = [];
  let fail = true;
  const port = { postMessage: (m) => sent.push(m) };
  const context = {
    Module: {
      _malloc: () => 16,
      _free: (p) => freed.push(p),
      _glob2_audio_capture: () => {
        if (fail) throw new Error('capture failure');
      },
      printErr: (error) => errors.push(error),
    },
    HEAPU8: new Uint8Array(8192),
    performance: { now: () => 0 },
    globalThis: {},
    console,
    URL,
    document: { baseURI: 'https://example.test/', hidden: false, addEventListener: () => {} },
    AudioContext: class {
      constructor() {
        this.state = 'running';
        this.currentTime = 0;
        this.audioWorklet = { addModule: async () => {} };
      }
    },
    AudioWorkletNode: class {
      constructor() {
        this.port = port;
      }
      connect() {}
    },
    Worker: class {
      postMessage() {}
    },
    MessageChannel: class {
      constructor() {
        this.port1 = {};
        this.port2 = {};
      }
    },
  };
  vm.createContext(context);
  vm.runInContext(fs.readFileSync(require.resolve('../audio'), 'utf8'), context);
  await context.Module.glob2Music.initialize();
  sent.length = 0;
  const captured = new ArrayBuffer(4096);
  port.onmessage({ data: { captured, time: 0 } });
  port.onmessage({ data: { captured, time: 1 } });
  fail = false;
  port.onmessage({ data: { captured, time: 2 } });
  assert.deepEqual(freed, [16, 16, 16]);
  assert.equal(errors.length, 1);
  assert.equal(sent.length, 3);
  assert.ok(sent.every((m) => m.capture === captured));
});
