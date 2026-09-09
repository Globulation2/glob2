const {test} = require('node:test');
const assert = require('node:assert/strict');
const activate = require('../audio');

test('audio activation ignores missing, running and closed contexts', async () => {
  await activate(undefined, assert.fail);
  for (const state of ['running', 'closed'])
    await activate({state, resume:assert.fail}, assert.fail);
});

test('closing audio during a pending resume does not reject the gesture handler', async () => {
  let reject;
  const context = {state:'suspended', resume:() => new Promise((_, fail) => { reject = fail; })};
  const pending = activate(context, assert.fail);
  context.state = 'closed';
  reject(new Error('Closed before resume completed'));
  await pending;
});

test('activation failures are reported and the next gesture can retry', async () => {
  const failure = new Error('Audio device unavailable');
  const errors = [];
  const context = {state:'suspended', resume:async () => { throw failure; }};
  await activate(context, error => errors.push(error));
  assert.deepEqual(errors, [failure]);
  assert.equal(context.state, 'suspended');
  context.resume = () => { context.state = 'running'; return Promise.resolve(); };
  const pending = activate(context, assert.fail);
  // resume must run synchronously in the gesture, before the first await.
  assert.equal(context.state, 'running');
  await pending;
});
