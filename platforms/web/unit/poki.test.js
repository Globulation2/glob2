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

test('a finished match runs one commercialBreak between gameplayStop and gameplayStart', async () => {
  const {win, calls, adapter, ad} = adFixture();
  const context = new win.AudioContext();
  await adapter.connect();
  adapter.session.start();
  assert.equal(win.glob2PortalHost.matchBreak('start'), false);
  win.glob2PortalHost.matchBreak('end');
  assert.equal(win.glob2PortalHost.matchBreak('leave'), true);
  assert.equal(context.state, 'suspended');
  assert.equal(win.glob2PortalHost.held, true);
  ad().resolve(); await settle();
  assert.equal(win.glob2PortalHost.held, false);
  assert.equal(context.state, 'running');
  // Starting the next match right away is the same break: no second ad.
  assert.equal(win.glob2PortalHost.matchBreak('start'), false);
  win.glob2PortalHost.matchBreak('end');
  win.glob2PortalHost.matchBreak('leave');
  win.timers.advance(180000);
  assert.equal(win.glob2PortalHost.matchBreak('start'), true);
  ad().resolve(); await settle();
  assert.deepEqual(calls, ['gameLoadingFinished', 'gameplayStart', 'gameplayStop', 'commercialBreak',
                           'gameplayStart', 'gameplayStop', 'commercialBreak', 'gameplayStart']);
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
