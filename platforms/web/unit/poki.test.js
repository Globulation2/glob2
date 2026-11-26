const {test} = require('node:test');
const assert = require('node:assert/strict');
const {create} = require('../poki');
const {fakeWindow} = require('./portal-fixture');

function fixture(init = () => Promise.resolve()) {
  const calls = [];
  const PokiSDK = {init: () => { calls.push('init'); return init(); }};
  for (const name of ['gameLoadingFinished', 'gameplayStart', 'gameplayStop'])
    PokiSDK[name] = () => calls.push(name);
  return {calls, adapter: create(fakeWindow({PokiSDK}))};
}

test('loading finishes once, after init and the ready menu', async () => {
  const {calls, adapter} = fixture();
  await adapter.connect();
  adapter.session.start(); adapter.session.start();
  assert.deepEqual(calls, ['init', 'gameLoadingFinished', 'gameplayStart']);
});

test('a menu ready before init is reported when init resolves', async () => {
  const {calls, adapter} = fixture();
  adapter.session.start();
  await adapter.connect();
  assert.deepEqual(calls, ['init', 'gameLoadingFinished', 'gameplayStart']);
});

test('gameplay stops while the page is hidden', async () => {
  const {calls, adapter} = fixture();
  await adapter.connect();
  adapter.session.start();
  adapter.session.visible(false);
  assert.deepEqual(calls.slice(-1), ['gameplayStop']);
});

test('a rejected init (ad blocker) leaves the game running without the SDK', async () => {
  const {calls, adapter} = fixture(() => Promise.reject(new Error('blocked')));
  assert.equal(await adapter.connect(), null);
  adapter.session.start();
  assert.deepEqual(calls, ['init']);
});
