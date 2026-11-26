const {test} = require('node:test');
const assert = require('node:assert/strict');
const {create} = require('../crazygames');
const {fakeWindow, settle} = require('./portal-fixture');

function fixture(init = () => Promise.resolve()) {
  const calls = [];
  const listeners = [];
  const game = {settings: {muteAudio: false}};
  for (const name of ['loadingStart', 'loadingStop', 'gameplayStart', 'gameplayStop'])
    game[name] = () => calls.push(name);
  game.addSettingsChangeListener = listener => listeners.push(listener);
  const win = fakeWindow({CrazyGames: {SDK: {init: () => { calls.push('init'); return init(); }, game}}});
  return {win, calls, listeners, adapter: create(win)};
}

test('loading and gameplay are reported only after init resolves', async () => {
  const {calls, adapter} = fixture();
  adapter.session.start();
  assert.deepEqual(calls, []);
  await adapter.connect();
  assert.deepEqual(calls, ['init', 'loadingStart', 'loadingStop', 'gameplayStart']);
});

test('loading stops when the menu is ready; tab switches report no gameplay change', async () => {
  const {calls, adapter} = fixture();
  await adapter.connect();
  adapter.session.start();
  adapter.session.visible(false); adapter.session.visible(true);
  assert.deepEqual(calls, ['init', 'loadingStart', 'loadingStop', 'gameplayStart']);
});

test('the mute audio setting silences the game', async () => {
  const {win, listeners, adapter} = fixture();
  const context = new win.AudioContext();
  await adapter.connect();
  listeners[0]({muteAudio: true});
  assert.equal(context.state, 'suspended');
  listeners[0]({muteAudio: false});
  assert.equal(context.state, 'running');
});

test('a rejected init leaves the game running without the SDK', async () => {
  const {calls, adapter} = fixture(() => Promise.reject(new Error('blocked')));
  assert.equal(await adapter.connect(), null);
  adapter.session.start();
  await settle();
  assert.deepEqual(calls, ['init']);
});

test('the adapter works without the SDK', async () => {
  const adapter = create(fakeWindow());
  assert.equal(await adapter.connect(), null);
  adapter.session.start();
});
