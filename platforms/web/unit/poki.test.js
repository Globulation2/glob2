const {test} = require('node:test');
const assert = require('node:assert/strict');
const {create} = require('../poki');
const {fakeWindow, adWindow, deferred, settle} = require('./portal-fixture');

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
  adapter.session.match(true);
  assert.deepEqual(calls, ['init', 'gameLoadingFinished', 'gameplayStart']);
});

test('a menu ready before init is reported when init resolves', async () => {
  const {calls, adapter} = fixture();
  adapter.session.start();
  adapter.session.match(true);
  await adapter.connect();
  assert.deepEqual(calls, ['init', 'gameLoadingFinished', 'gameplayStart']);
});

test('gameplay stops while the page is hidden', async () => {
  const {calls, adapter} = fixture();
  await adapter.connect();
  adapter.session.start();
  adapter.session.match(true);
  adapter.session.visible(false);
  assert.deepEqual(calls.slice(-1), ['gameplayStop']);
});

test('a rejected init (ad blocker) leaves the game running without the SDK', async () => {
  const {calls, adapter} = fixture(() => Promise.reject(new Error('blocked')));
  assert.equal(await adapter.connect(), null);
  adapter.session.start();
  assert.deepEqual(calls, ['init']);
});

function adFixture() {
  const calls = [];
  let ad = null;
  const PokiSDK = {init: () => Promise.resolve()};
  for (const name of ['gameLoadingFinished', 'gameplayStart', 'gameplayStop'])
    PokiSDK[name] = () => calls.push(name);
  PokiSDK.commercialBreak = onStart => { calls.push('commercialBreak'); ad = deferred(); onStart(); return ad.promise; };
  const win = adWindow('poki', {PokiSDK});
  return {win, calls, adapter: create(win), ad: () => ad};
}

test('every match start, the first included, runs commercialBreak between gameplayStop and gameplayStart', async () => {
  const {win, calls, adapter, ad} = adFixture();
  const context = new win.AudioContext();
  await adapter.connect();
  adapter.session.start();
  assert.equal(win.glob2PortalHost.matchBreak('start'), true);
  assert.equal(context.state, 'suspended');
  assert.equal(win.glob2PortalHost.held, true);
  ad().resolve(); await settle();
  assert.equal(win.glob2PortalHost.held, false);
  assert.equal(context.state, 'running');
  win.glob2PortalHost.matchBreak('end');
  // Returning to the menu is not a return to play: no break there.
  assert.equal(win.glob2PortalHost.matchBreak('leave'), false);
  assert.equal(win.glob2PortalHost.held, false);
  // No gap of ours: the next start asks again at once and Poki decides.
  assert.equal(win.glob2PortalHost.matchBreak('start'), true);
  ad().resolve(); await settle();
  assert.deepEqual(calls, ['gameLoadingFinished', 'commercialBreak', 'gameplayStart', 'gameplayStop',
                           'commercialBreak', 'gameplayStart']);
  assert.equal(adapter.breaks.requests, 2);
});

test('without the Poki SDK a break holds nothing', async () => {
  const win = adWindow('poki');
  const adapter = create(win);
  await adapter.connect();
  adapter.session.start();
  win.glob2PortalHost.matchBreak('start');
  assert.equal(win.glob2PortalHost.matchBreak('leave'), false);
  assert.equal(win.glob2PortalHost.held, false);
});

test('resuming from the in-game menu or a pause runs commercialBreak before gameplayStart', async () => {
  const {win, calls, adapter, ad} = adFixture();
  const context = new win.AudioContext();
  await adapter.connect();
  adapter.session.start();
  win.glob2PortalHost.matchBreak('start');
  ad().resolve(); await settle();
  calls.length = 0;
  assert.equal(win.glob2PortalHost.matchBreak('pause'), false);
  assert.equal(win.glob2PortalHost.matchBreak('resume'), true);
  assert.equal(win.glob2PortalHost.held, true);
  assert.equal(context.state, 'suspended');
  assert.equal(adapter.session.paused, false);
  assert.equal(adapter.session.active, false);
  ad().resolve(); await settle();
  assert.equal(win.glob2PortalHost.held, false);
  assert.equal(context.state, 'running');
  assert.deepEqual(calls, ['gameplayStop', 'commercialBreak', 'gameplayStart']);
  // A pause ended by leaving the match is no return to play.
  win.glob2PortalHost.matchBreak('pause');
  win.glob2PortalHost.matchBreak('end');
  assert.equal(win.glob2PortalHost.matchBreak('leave'), false);
  assert.deepEqual(calls.slice(-2), ['gameplayStart', 'gameplayStop']);
});

test('a resume outside a paused match asks for nothing', async () => {
  const {win, calls, adapter} = adFixture();
  await adapter.connect();
  adapter.session.start();
  assert.equal(win.glob2PortalHost.matchBreak('resume'), false);
  assert.deepEqual(calls, ['gameLoadingFinished']);
});
