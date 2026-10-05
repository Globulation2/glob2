// SPDX-License-Identifier: GPL-3.0-or-later
const { test } = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const vm = require('node:vm');

async function worker() {
  const calls = [],
    sent = [],
    notifications = [],
    timers = new Map();
  let nextTimer = 0;
  let position = 5,
    reply = { position: 2.125, generation: 1 };
  const port = {
    start() {},
    postMessage(message) {
      sent.push(message);
      if (message.positionRequest && reply)
        queueMicrotask(() =>
          port.onmessage({ data: { positionRequest: message.positionRequest, ...reply } }),
        );
    },
  };
  const runtime = {
    UTF8ToString: (value) => value,
    _audio_snapshot: () => JSON.stringify({ position, preview: true }),
    _audio_replace: () => true,
    _audio_stage() {},
    _malloc: () => 8,
    _free() {},
    HEAPU8: new Uint8Array(64),
    _audio_control: (command, value) => {
      calls.push([command, value]);
      if (command === 2) position = value;
    },
  };
  const context = {
    importScripts() {},
    createMusicRuntime: async () => runtime,
    postMessage: (message) => notifications.push(message),
    setTimeout: (fn) => {
      timers.set(++nextTimer, fn);
      return nextTimer;
    },
    clearTimeout: (id) => timers.delete(id),
    setInterval() {},
    performance: { now: () => 0 },
  };
  vm.createContext(context);
  vm.runInContext(fs.readFileSync(require.resolve('../music-worker'), 'utf8'), context);
  async function send(message) {
    context.onmessage({ data: message });
    await vm.runInContext('chain', context);
  }
  await send({ type: 'initialize', port });
  await send({ type: 'preview', tracks: [] });
  return {
    send,
    calls,
    sent,
    notifications,
    timers,
    port,
    reply: (value) => {
      reply = value;
    },
  };
}

test('preview Play only pauses consumption and preserves prepared decoder state', async () => {
  const { send, calls, sent } = await worker();
  assert.deepEqual(calls, [[0, 1]]);
  const resets = () => sent.filter((message) => message.reset).length;
  const before = resets();
  await send({ type: 'control', index: 0, value: 1 });
  await send({ type: 'control', index: 0, value: 0 });
  await send({ type: 'control', index: 0, value: 1 });
  assert.equal(resets(), before);
  assert.deepEqual(calls, [[0, 1]]);
  assert.deepEqual(
    sent.filter((message) => message.paused !== undefined).map((message) => message.paused),
    [true, false, true, false],
  );
});

test('preview Reset uses consumed position and preserves paused state', async () => {
  const { send, calls, sent } = await worker();
  await send({ type: 'control', index: 6, value: 0 });
  assert.deepEqual(calls.slice(-2), [
    [2, 2.125],
    [6, 0],
  ]);
  assert.equal(sent.at(-1).paused, true);
});

test('Reset cannot rewind a newer Seek to stale consumer metadata', async () => {
  const { send, calls, reply } = await worker();
  await send({ type: 'control', index: 2, value: 12 });
  reply({ position: 2.125, generation: 1 });
  await send({ type: 'control', index: 6, value: 0 });
  assert.deepEqual(calls.slice(-2), [
    [2, 12],
    [6, 0],
  ]);
});

test('a suspended worklet cannot retain the command credit or leave preview paused', async () => {
  const { send, calls, sent, notifications, timers, port, reply } = await worker();
  await send({ type: 'control', index: 0, value: 1 });
  reply(null);
  const resetting = send({ type: 'control', index: 6, value: 0 });
  await new Promise((resolve) => setImmediate(resolve));
  assert.equal(timers.size, 1);
  for (const expire of timers.values()) expire();
  await resetting;
  assert.match(notifications.at(-2).error, /did not acknowledge/);
  assert.equal(notifications.at(-1).ack, true);
  assert.equal(sent.at(-1).paused, false);
  assert.deepEqual(calls, [[0, 1]]);
  // The response can arrive after resume, but its rejected reset is gone.
  const request = sent.find((message) => message.positionRequest).positionRequest;
  port.onmessage({ data: { positionRequest: request, position: 9, generation: 1 } });
  await send({ type: 'control', index: 1, value: 2 });
  assert.deepEqual(calls.at(-1), [1, 2]);
});
