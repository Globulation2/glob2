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
  adapter.session.match(true);
  assert.deepEqual(calls, []);
  await adapter.connect();
  assert.deepEqual(calls, ['init', 'loadingStart', 'loadingStop', 'gameplayStart']);
});

test('loading stops when the menu is ready; tab switches report no gameplay change', async () => {
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

test('entering the menu or pausing stops gameplay; resuming starts it; no midgame ad', async () => {
  const {win, calls, adapter} = adFixture();
  await adapter.connect();
  adapter.session.start();
  for (const point of ['start', 'pause', 'resume', 'pause', 'resume', 'pause', 'end'])
    win.glob2PortalHost.matchBreak(point);
  assert.deepEqual(calls, ['loadingStart', 'loadingStop', 'gameplayStart', 'gameplayStop', 'gameplayStart',
                           'gameplayStop', 'gameplayStart', 'gameplayStop']);
  assert.equal(win.glob2PortalHost.matchBreak('leave'), true);
  assert.deepEqual(calls.slice(-1), ['requestAd midgame']);
});

function deviceFixture(type) {
  const events = [];
  const game = {settings: {muteAudio: false}};
  const SDK = {init: () => Promise.resolve(), game, user: {systemInfo: {device: {type}}}};
  const win = fakeWindow({CrazyGames: {SDK}, Event: class { constructor(name) { this.type = name; } },
                          dispatchEvent: event => events.push(event.type)});
  return {win, events, adapter: create(win)};
}

test('touch presentation follows the SDK device type, not touch detection', async () => {
  for (const [type, touch] of [['mobile', true], ['tablet', true], ['desktop', false]]) {
    const {win, events, adapter} = deviceFixture(type);
    await adapter.connect();
    assert.deepEqual(win.glob2PresentationOverride, {touch}, type);
    assert.deepEqual(events, ['glob2-presentation-override']);
  }
});

test('an unknown or missing device type leaves the shell to decide', async () => {
  for (const type of [undefined, 'console']) {
    const {win, events, adapter} = deviceFixture(type);
    await adapter.connect();
    assert.equal(win.glob2PresentationOverride, undefined);
    assert.deepEqual(events, []);
  }
});

test('shell.html prefers the portal presentation override and republishes on its event', () => {
  const shell = require('node:fs').readFileSync(require('node:path').join(__dirname, '../shell.html'), 'utf8');
  assert.match(shell, /typeof window\.glob2PresentationOverride\?\.touch === 'boolean'/);
  assert.match(shell, /addEventListener\('glob2-presentation-override',publishPresentationMetrics\)/);
});
