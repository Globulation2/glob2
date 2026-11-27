const {test} = require('node:test');
const assert = require('node:assert/strict');
const {create, attach} = require('../gamepix');
const {adWindow, deferred, settle} = require('./portal-fixture');

class FakeContext {
  constructor() { this.state = 'running'; this.listeners = []; }
  addEventListener(name, listener) { if (name === 'statechange') this.listeners.push(listener); }
  set(state) { this.state = state; this.listeners.forEach(listener => listener()); }
  suspend() { this.set('suspended'); return Promise.resolve(); }
  resume() { this.set('running'); return Promise.resolve(); }
  close() { this.state = 'closed'; return Promise.resolve(); }
}

function fixture(hidden = false) {
  const calls = [];
  const win = {
    AudioContext: FakeContext,
    document: {hidden},
    GamePix: {lifecycle: {loading: value => calls.push(['loading', value]), loaded: () => calls.push(['loaded']),
                          gameStart: () => calls.push(['gameStart'])}},
  };
  return {win, calls, adapter: create(win)};
}

test('pause and resume silence every game audio context and are idempotent', () => {
  const {win, adapter} = fixture();
  const music = new win.AudioContext(), effects = new win.AudioContext();
  adapter.pause(); adapter.pause();
  assert.deepEqual([music.state, effects.state], ['suspended', 'suspended']);
  adapter.resume(); adapter.resume();
  assert.deepEqual([music.state, effects.state], ['running', 'running']);
});

test('audio the game reactivates during an ad stays silent until GamePix resumes', () => {
  const {win, adapter} = fixture();
  const context = new win.AudioContext();
  adapter.pause();
  context.resume();
  assert.equal(context.state, 'suspended');
  adapter.resume();
  assert.equal(context.state, 'running');
});

test('resume leaves a hidden page suspended for the game to reactivate', () => {
  const {win, adapter} = fixture(true);
  const context = new win.AudioContext();
  adapter.pause(); adapter.resume();
  assert.equal(context.state, 'suspended');
  assert.equal(adapter.paused, false);
});

test('closed contexts are forgotten', () => {
  const {win, adapter} = fixture();
  const context = new win.AudioContext();
  context.close();
  assert.equal(adapter.contexts.size, 0);
});

test('loading reports increasing whole percentages, then loaded once at the main menu', () => {
  const {calls, adapter} = fixture();
  adapter.progress('12.7'); adapter.progress(12); adapter.progress(5); adapter.progress(140);
  adapter.start(); adapter.start(); adapter.progress(50);
  assert.deepEqual(calls, [['loading', 12], ['loading', 100], ['loaded']]);
  assert.equal(adapter.started, false);
});

test('gameStart fires once, when the first match is playing', () => {
  const {calls, adapter} = fixture();
  adapter.start();
  adapter.session.match(true);
  adapter.session.match(false);
  adapter.session.match(true);
  assert.deepEqual(calls, [['loading', 100], ['loaded'], ['gameStart']]);
  assert.equal(adapter.started, true);
});

test('a GamePix pause holds the game as well as silencing it, idempotently', () => {
  const {win, adapter} = fixture();
  const context = new win.AudioContext();
  adapter.start();
  adapter.session.match(true);
  adapter.pause(); adapter.pause();
  assert.equal(adapter.session.held, true);
  assert.equal(adapter.session.active, false);
  assert.equal(context.state, 'suspended');
  adapter.resume(); adapter.resume();
  assert.equal(adapter.session.held, false);
  assert.equal(adapter.session.active, true);
  assert.equal(context.state, 'running');
});

test('a GamePix pause before the first match delays gameStart until play', () => {
  const {calls, adapter} = fixture();
  adapter.start();
  adapter.pause();
  adapter.session.match(true);
  assert.deepEqual(calls.slice(-1), [['loaded']]);
  adapter.resume();
  assert.deepEqual(calls.slice(-1), [['gameStart']]);
});

test('the adapter works without the SDK', () => {
  const win = {document: {hidden: false}};
  const adapter = create(win);
  adapter.progress(10); adapter.start(); adapter.pause(); adapter.resume();
});

function adFixture(extra) {
  const win = adWindow('gamepix', extra);
  return {win, adapter: create(win)};
}

test('a finished match shows one interstitial, at most once every three minutes', async () => {
  let ad = null;
  const shown = [];
  const handlers = {};
  const {win, adapter} = adFixture({GamePix: {
    lifecycle: {loading() {}, loaded() {}, gameStart() {}},
    events: {on: (name, handler) => { handlers[name] = handler; }},
    ads: {interstitial: () => { shown.push('interstitial'); handlers.pause(); ad = deferred(); return ad.promise; }},
  }});
  win.document.getElementById = () => null;
  win.document.body = {classList: {contains: () => false}};
  win.document.addEventListener = () => {};
  win.MutationObserver = class { observe() {} };
  attach(win, adapter);
  const context = new win.AudioContext();
  adapter.session.start();
  assert.equal(win.glob2PortalHost.matchBreak('start'), false);
  win.glob2PortalHost.matchBreak('end');
  assert.equal(win.glob2PortalHost.matchBreak('leave'), true);
  assert.equal(context.state, 'suspended');
  handlers.resume();
  ad.resolve({success: true}); await settle();
  assert.equal(win.glob2PortalHost.held, false);
  assert.equal(context.state, 'running');
  assert.equal(win.glob2PortalHost.matchBreak('start'), false);
  assert.deepEqual(shown, ['interstitial']);
});

test('without the GamePix SDK a break holds nothing', () => {
  const {win} = adFixture();
  win.glob2PortalHost.matchBreak('start');
  assert.equal(win.glob2PortalHost.matchBreak('leave'), false);
});
