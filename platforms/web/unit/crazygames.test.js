const {test} = require('node:test');
const assert = require('node:assert/strict');
const {create} = require('../crazygames');
const {fakeWindow, adWindow, settle} = require('./portal-fixture');

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

// The QA tool's load time runs from Play Now to the first gameplayStart.
test('the first gameplayStart comes with loadingStop at the first interactive screen', async () => {
  const {calls, adapter} = fixture();
  await adapter.connect();
  assert.deepEqual(calls, ['init', 'loadingStart']);
  adapter.session.start();
  assert.deepEqual(calls, ['init', 'loadingStart', 'loadingStop', 'gameplayStart']);
  // The first match continues it; its end is the first break.
  adapter.session.match(true);
  assert.deepEqual(calls.slice(4), []);
  adapter.session.match(false);
  assert.deepEqual(calls.slice(4), ['gameplayStop']);
  adapter.session.match(true); adapter.session.match(false);
  assert.deepEqual(calls.slice(4), ['gameplayStop', 'gameplayStart', 'gameplayStop']);
});

test('tab switches report no gameplay change', async () => {
  const {calls, adapter} = fixture();
  await adapter.connect();
  adapter.session.start();
  adapter.session.match(true);
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

function adFixture(outcome) {
  const calls = [];
  const game = {settings: {muteAudio: false}, addSettingsChangeListener() {}};
  for (const name of ['loadingStart', 'loadingStop', 'gameplayStart', 'gameplayStop'])
    game[name] = () => calls.push(name);
  const callbacks = {};
  const ad = {requestAd: (type, handlers) => { calls.push('requestAd ' + type); Object.assign(callbacks, handlers); }};
  const win = adWindow('crazygames', {CrazyGames: {SDK: {init: () => Promise.resolve(), game, ad}}});
  return {win, calls, callbacks, adapter: create(win)};
}

test('a finished match requests one midgame ad; audio mutes only once it starts', async () => {
  const {win, calls, callbacks, adapter} = adFixture();
  const context = new win.AudioContext();
  await adapter.connect();
  adapter.session.start();
  assert.equal(win.glob2PortalHost.matchBreak('start'), false);
  win.glob2PortalHost.matchBreak('end');
  assert.equal(win.glob2PortalHost.matchBreak('leave'), true);
  assert.equal(win.glob2PortalHost.held, true);
  assert.equal(context.state, 'running');
  callbacks.adStarted();
  assert.equal(context.state, 'suspended');
  callbacks.adFinished(); await settle();
  assert.equal(win.glob2PortalHost.held, false);
  assert.equal(context.state, 'running');
  assert.deepEqual(calls.filter(call => call.startsWith('requestAd')), ['requestAd midgame']);
});

test('gameplay stops before the midgame ad and the next match starts it again', async () => {
  const {win, calls, callbacks, adapter} = adFixture();
  await adapter.connect();
  adapter.session.start();
  win.glob2PortalHost.matchBreak('start');
  win.glob2PortalHost.matchBreak('end');
  win.glob2PortalHost.matchBreak('leave');
  callbacks.adStarted(); callbacks.adFinished(); await settle();
  // Back in the menu after the ad: no gameplay until the next match.
  assert.deepEqual(calls, ['loadingStart', 'loadingStop', 'gameplayStart', 'gameplayStop', 'requestAd midgame']);
  // Within three minutes of that break the next start runs no ad.
  assert.equal(win.glob2PortalHost.matchBreak('start'), false);
  assert.deepEqual(calls.slice(5), ['gameplayStart']);
});

test('adError (adsDisabled in Basic Launch, adCooldown, adblock) ends the break', async () => {
  const {win, callbacks, adapter} = adFixture();
  await adapter.connect();
  adapter.session.start();
  win.glob2PortalHost.matchBreak('start');
  win.glob2PortalHost.matchBreak('leave');
  callbacks.adError({code: 'adsDisabled', message: 'Basic Launch'}); await settle();
  assert.equal(win.glob2PortalHost.held, false);
});

test('without the CrazyGames SDK a break holds nothing', async () => {
  const win = adWindow('crazygames');
  const adapter = create(win);
  await adapter.connect();
  win.glob2PortalHost.matchBreak('start');
  assert.equal(win.glob2PortalHost.matchBreak('leave'), false);
});
