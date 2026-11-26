const {test} = require('node:test');
const assert = require('node:assert/strict');
const {create} = require('../gamedistribution');
const {fakeWindow} = require('./portal-fixture');

test('the adapter keeps the packaged game ID and adds an event handler', () => {
  const win = fakeWindow({GD_OPTIONS: {gameId: 'abc'}});
  create(win);
  assert.equal(win.GD_OPTIONS.gameId, 'abc');
  assert.equal(typeof win.GD_OPTIONS.onEvent, 'function');
});

test('SDK_GAME_PAUSE silences the game until SDK_GAME_START', () => {
  const win = fakeWindow({GD_OPTIONS: {gameId: 'abc'}});
  const adapter = create(win);
  const context = new win.AudioContext();
  adapter.session.start();
  win.GD_OPTIONS.onEvent({name: 'SDK_GAME_PAUSE'});
  assert.equal(context.state, 'suspended');
  assert.equal(adapter.session.active, false);
  win.GD_OPTIONS.onEvent({name: 'SDK_READY'});
  assert.equal(context.state, 'suspended');
  win.GD_OPTIONS.onEvent({name: 'SDK_GAME_START'});
  assert.equal(context.state, 'running');
  assert.equal(adapter.session.active, true);
});

test('an existing onEvent handler still receives events', () => {
  const seen = [];
  const win = fakeWindow({GD_OPTIONS: {gameId: 'abc', onEvent: event => seen.push(event.name)}});
  create(win);
  win.GD_OPTIONS.onEvent({name: 'SDK_GAME_PAUSE'});
  assert.deepEqual(seen, ['SDK_GAME_PAUSE']);
});

test('the adapter creates GD_OPTIONS when the packager did not', () => {
  const win = fakeWindow();
  create(win);
  assert.equal(typeof win.GD_OPTIONS.onEvent, 'function');
});
