const {test} = require('node:test');
const assert = require('node:assert/strict');
const {createAudio, createSession, createBreaks, installHost, watchShell} = require('../portal-core');
const {fakeWindow, adWindow, deferred, settle} = require('./portal-fixture');

test('every reason must clear before audio resumes', () => {
  const win = fakeWindow();
  const audio = createAudio(win);
  const context = new win.AudioContext();
  audio.set('muted', true); audio.pause();
  audio.resume();
  assert.equal(context.state, 'suspended');
  audio.set('muted', false);
  assert.equal(context.state, 'running');
});

test('the audio tracker is created once per window', () => {
  const win = fakeWindow();
  assert.equal(createAudio(win), createAudio(win));
});

test('gameplay follows loaded, in a match, visible and not held', () => {
  const events = [];
  const session = createSession({gameplay: active => events.push(active), loaded: () => events.push('loaded')});
  session.visible(true); session.hold(true);
  session.start();
  session.match(true);
  session.hold(false); session.visible(false); session.visible(false); session.visible(true);
  assert.deepEqual(events, ['loaded', true, false, true]);
});

test('watchShell reads the download bar, ready state and visibility', () => {
  const listeners = {};
  const observers = [];
  const bar = {value: '0', getAttribute: () => bar.value};
  const body = {classes: new Set(), classList: {contains: name => body.classes.has(name)}};
  const win = {
    document: {hidden: false, body, getElementById: id => (id === 'loading-bar' ? bar : null),
               addEventListener: (name, listener) => { listeners[name] = listener; }},
    MutationObserver: class { constructor(callback) { observers.push(callback); } observe() {} },
  };
  const calls = [];
  const session = createSession({progress: value => calls.push(value), loaded: () => calls.push('loaded'),
                                 gameplay: active => calls.push(active)});
  watchShell(win, session);
  bar.value = '40'; observers[0]();
  body.classes.add('ready'); observers[0]();
  session.match(true);
  win.document.hidden = true; listeners.visibilitychange();
  assert.deepEqual(calls, [0, 40, 100, 'loaded', true, false]);
});

test('the main menu is not gameplay', () => {
  const events = [];
  const session = createSession({gameplay: active => events.push(active)});
  session.start();
  assert.deepEqual(events, []);
  session.match(true); session.match(false);
  assert.deepEqual(events, [true, false]);
});

test('holds are reasons: a portal pause outlasts the end of an ad', () => {
  const session = createSession();
  session.start(); session.match(true);
  session.hold(true, 'ad'); session.hold(true);
  session.hold(false, 'ad');
  assert.equal(session.active, false);
  session.hold(false);
  assert.equal(session.active, true);
});

function breaksFixture(options = {}, win = adWindow('test')) {
  const audio = createAudio(win);
  const gameplay = [];
  const session = createSession({gameplay: active => gameplay.push(active)});
  session.start();
  const shown = [];
  let ad = deferred();
  const breaks = createBreaks(win, {session, audio, show: (handle, point) => {
    shown.push(point); ad = deferred(); ad.handle = handle; return ad.promise;
  }, ...options});
  const context = new win.AudioContext();
  return {win, audio, session, breaks, shown, gameplay, context, ad: () => ad};
}

test('no break runs before the first match; leaving a match runs one and holds the game until it ends', async () => {
  const {win, breaks, shown, gameplay, context, session, ad} = breaksFixture();
  assert.equal(breaks.matchBreak('start'), false);
  assert.deepEqual(gameplay, [true]);
  assert.equal(breaks.matchBreak('end'), false);
  assert.equal(breaks.matchBreak('leave'), true);
  assert.deepEqual(shown, ['leave']);
  assert.equal(breaks.held, true);
  assert.equal(context.state, 'suspended');
  ad().resolve(); await settle();
  assert.equal(breaks.held, false);
  assert.equal(context.state, 'running');
  win.timers.advance(180000);
  assert.equal(breaks.matchBreak('start'), true);
  assert.equal(session.active, false);
  ad().resolve(); await settle();
  assert.equal(session.active, true);
  assert.deepEqual(gameplay, [true, false, true]);
  assert.equal(breaks.requests, 2);
});

test('firstStart allows a break before the first match', () => {
  const {breaks, shown} = breaksFixture({firstStart: true});
  assert.equal(breaks.matchBreak('start'), true);
  assert.deepEqual(shown, ['start']);
});

test('a package whose policy has no ads never asks for one', () => {
  const win = fakeWindow({GLOB2_PORTAL: {name: 'newgrounds', externalLinks: true, onlineSignIn: true, ads: false}});
  const {breaks, shown} = breaksFixture({}, win);
  breaks.matchBreak('start'); breaks.matchBreak('leave'); breaks.matchBreak('start');
  assert.deepEqual(shown, []);
  assert.equal(breaks.enabled, false);
});

test('without the SDK (null, a throw or an ad blocker) the game is never held', async () => {
  const win = adWindow('test');
  const session = createSession();
  const audio = createAudio(win);
  const context = new win.AudioContext();
  for (const show of [() => null, () => { throw new Error('blocked'); }, () => Promise.reject(new Error('blocked'))]) {
    const breaks = createBreaks(win, {session, audio, show, firstStart: true});
    breaks.matchBreak('start');
    await settle();
    assert.equal(breaks.held, false);
    assert.equal(context.state, 'running');
  }
});

test('muteOnStart keeps audio until the ad really plays', async () => {
  const {breaks, context, ad} = breaksFixture({muteOnStart: true});
  breaks.matchBreak('leave');
  assert.equal(breaks.held, true);
  assert.equal(context.state, 'running');
  ad().handle.started();
  assert.equal(context.state, 'suspended');
  ad().resolve(); await settle();
  assert.equal(context.state, 'running');
});

test('minInterval spaces our own requests', () => {
  let clock = 0;
  const {breaks, shown, ad} = breaksFixture({minInterval: 1000, now: () => clock});
  breaks.matchBreak('leave');
  breaks.finished();
  clock = 500; breaks.matchBreak('leave');
  clock = 1000; breaks.matchBreak('leave');
  assert.deepEqual(shown, ['leave', 'leave']);
  ad().resolve();
});

test('one break at a time', () => {
  const {breaks, shown} = breaksFixture();
  assert.equal(breaks.matchBreak('leave'), true);
  assert.equal(breaks.matchBreak('leave'), false);
  assert.deepEqual(shown, ['leave']);
});

test('a stalled SDK releases the game: before the ad starts and after it runs too long', () => {
  const {win, breaks, context, ad} = breaksFixture({startTimeout: 100, maxDuration: 1000});
  breaks.matchBreak('leave');
  win.timers.advance(100);
  assert.equal(breaks.held, false);
  assert.equal(context.state, 'running');
  win.timers.advance(180000);
  breaks.matchBreak('leave');
  ad().handle.started();
  win.timers.advance(999);
  assert.equal(breaks.held, true);
  win.timers.advance(1);
  assert.equal(breaks.held, false);
  assert.equal(win.timers.pending, 0);
});

test('installHost gives the game matchBreak and held', async () => {
  const {win, breaks, ad} = breaksFixture();
  const host = installHost(win, breaks);
  assert.equal(win.glob2PortalHost, host);
  assert.equal(host.matchBreak('leave'), true);
  assert.equal(host.held, true);
  ad().resolve(); await settle();
  assert.equal(host.held, false);
});

test('leaving a match and starting the next within three minutes is one break', async () => {
  const {win, breaks, shown, ad} = breaksFixture();
  breaks.matchBreak('start'); breaks.matchBreak('end');
  assert.equal(breaks.matchBreak('leave'), true);
  win.timers.advance(5000);
  ad().resolve(); await settle();
  win.timers.advance(179999);
  assert.equal(breaks.matchBreak('start'), false);
  assert.deepEqual(shown, ['leave']);
});

test('the three minutes run from the end of the break, then a start asks again', async () => {
  const {win, breaks, shown, ad} = breaksFixture();
  breaks.matchBreak('start'); breaks.matchBreak('end');
  breaks.matchBreak('leave');
  win.timers.advance(5000);
  ad().resolve(); await settle();
  win.timers.advance(180000);
  assert.equal(breaks.matchBreak('start'), true);
  assert.deepEqual(shown, ['leave', 'start']);
});

test('a break whose ad never started still starts the three minutes', async () => {
  const {win, breaks, shown, ad} = breaksFixture();
  breaks.matchBreak('start'); breaks.matchBreak('end');
  breaks.matchBreak('leave');
  ad().resolve(false); await settle();
  assert.equal(breaks.matchBreak('start'), false);
  win.timers.advance(180000);
  assert.equal(breaks.matchBreak('start'), true);
  assert.deepEqual(shown, ['leave', 'start']);
});

test('no SDK to ask is not a break and starts no clock', () => {
  const win = adWindow('test');
  let available = false;
  const shown = [];
  const breaks = createBreaks(win, {session: createSession(), audio: createAudio(win), firstStart: true,
    show: () => available ? (shown.push('ad'), new Promise(() => {})) : null});
  assert.equal(breaks.matchBreak('start'), false);
  available = true;
  assert.equal(breaks.matchBreak('leave'), true);
  assert.deepEqual(shown, ['ad']);
});

// The game reports the in-game menu and pauses as 'pause' and 'resume'.
test('pause and resume stop and restart gameplay once each and never run a break', () => {
  const {breaks, shown, gameplay, session} = breaksFixture();
  breaks.matchBreak('start');
  assert.equal(breaks.matchBreak('pause'), false);
  assert.equal(breaks.matchBreak('pause'), false);
  assert.equal(breaks.matchBreak('resume'), false);
  assert.equal(breaks.matchBreak('resume'), false);
  breaks.matchBreak('pause'); breaks.matchBreak('resume');
  assert.deepEqual(gameplay, [true, false, true, false, true]);
  assert.deepEqual(shown, []);
  assert.equal(breaks.held, false);
  assert.equal(breaks.requests, 0);
  assert.equal(session.paused, false);
});

test('leaving a match from the menu sends one stop; the next match starts in play', () => {
  const {win, breaks, shown, gameplay} = breaksFixture();
  breaks.matchBreak('start');
  breaks.matchBreak('pause');
  breaks.matchBreak('end');
  assert.deepEqual(gameplay, [true, false]);
  assert.equal(breaks.matchBreak('leave'), true);
  assert.deepEqual(shown, ['leave']);
});

test('a pause outside a match changes nothing; an ad release does not restart a paused match', async () => {
  const {win, breaks, gameplay, session, ad} = breaksFixture();
  breaks.matchBreak('pause');
  assert.deepEqual(gameplay, []);
  assert.equal(session.paused, false);
  // First match, finished, left (break), then the next start after 3 minutes runs a break.
  breaks.matchBreak('start'); breaks.matchBreak('end'); breaks.matchBreak('leave');
  ad().resolve(); await settle();
  win.timers.advance(180000);
  assert.equal(breaks.matchBreak('start'), true);
  breaks.matchBreak('pause');
  ad().resolve(); await settle();
  assert.deepEqual(gameplay, [true, false]);
  breaks.matchBreak('resume');
  assert.deepEqual(gameplay, [true, false, true]);
});

test('pauses do not start or reset the three minutes between breaks', async () => {
  const {win, breaks, shown, ad} = breaksFixture();
  breaks.matchBreak('start'); breaks.matchBreak('end');
  assert.equal(breaks.matchBreak('leave'), true);
  ad().resolve(); await settle();
  breaks.matchBreak('start');
  win.timers.advance(120000);
  breaks.matchBreak('pause'); breaks.matchBreak('resume');
  win.timers.advance(60000);
  breaks.matchBreak('end');
  assert.equal(breaks.matchBreak('leave'), true);
  assert.deepEqual(shown, ['leave', 'leave']);
});

test('points choose where a break may ask: Poki asks at start and resume, never at leave', async () => {
  const {breaks, shown, gameplay, ad} = breaksFixture({points: ['start', 'resume'], firstStart: true, minInterval: 0});
  assert.equal(breaks.matchBreak('start'), true);
  ad().resolve(); await settle();
  assert.equal(breaks.matchBreak('pause'), false);
  assert.equal(breaks.matchBreak('resume'), true);
  assert.equal(breaks.held, true);
  ad().resolve(); await settle();
  breaks.matchBreak('end');
  assert.equal(breaks.matchBreak('leave'), false);
  assert.equal(breaks.matchBreak('start'), true);
  ad().resolve(); await settle();
  assert.deepEqual(shown, ['start', 'resume', 'start']);
  // Each return to play is one stop and one start, the start after the break.
  assert.deepEqual(gameplay, [true, false, true, false, true]);
});

test('by default resume never asks and leave does', () => {
  const {breaks, shown} = breaksFixture();
  breaks.matchBreak('start'); breaks.matchBreak('pause'); breaks.matchBreak('resume');
  breaks.matchBreak('end');
  assert.equal(breaks.matchBreak('leave'), true);
  assert.deepEqual(shown, ['leave']);
});

test('a point that is not listed asks nothing and starts no clock', async () => {
  const {win, breaks, shown, ad} = breaksFixture({points: ['start']});
  breaks.matchBreak('start'); breaks.matchBreak('end');
  assert.equal(breaks.matchBreak('leave'), false);
  win.timers.advance(1);
  assert.equal(breaks.matchBreak('start'), true);
  ad().resolve(); await settle();
  assert.deepEqual(shown, ['start']);
});

test('minInterval 0 leaves the frequency to the SDK', async () => {
  const {breaks, shown, ad} = breaksFixture({minInterval: 0});
  breaks.matchBreak('start'); breaks.matchBreak('end');
  breaks.matchBreak('leave');
  ad().resolve(); await settle();
  assert.equal(breaks.matchBreak('start'), true);
  assert.deepEqual(shown, ['leave', 'start']);
});

test('ad.active tells the adapter whether its break still waits', () => {
  const {win, breaks, ad} = breaksFixture({startTimeout: 100});
  breaks.matchBreak('start'); breaks.matchBreak('end'); breaks.matchBreak('leave');
  const handle = ad().handle;
  assert.equal(handle.active, true);
  win.timers.advance(100);
  assert.equal(handle.active, false);
});

function focusWindow(hasFocus = true) {
  const listeners = {}, docListeners = {};
  const win = fakeWindow({addEventListener: (name, listener) => { listeners[name] = listener; }});
  win.document = {hidden: false, body: null, hasFocus: () => hasFocus,
                  addEventListener: (name, listener) => { docListeners[name] = listener; }};
  return {win, listeners, docListeners};
}

test('watchShell with focus treats a window without focus as hidden and silences audio', () => {
  const {win, listeners, docListeners} = focusWindow();
  const audio = createAudio(win);
  const context = new win.AudioContext();
  const gameplay = [];
  const session = createSession({gameplay: active => gameplay.push(active)});
  watchShell(win, session, {focus: true, audio});
  session.start(); session.match(true);
  listeners.blur();
  assert.equal(context.state, 'suspended');
  assert.equal(session.active, false);
  // Focus alone does not show a hidden tab.
  win.document.hidden = true; docListeners.visibilitychange();
  listeners.focus();
  assert.equal(session.active, false);
  win.document.hidden = false; docListeners.visibilitychange();
  assert.equal(session.active, true);
  assert.equal(context.state, 'running');
  assert.deepEqual(gameplay, [true, false, true]);
});

test('watchShell with focus starts silent when the page loads without focus', () => {
  const {win, listeners} = focusWindow(false);
  const audio = createAudio(win);
  const session = createSession();
  watchShell(win, session, {focus: true, audio});
  const context = new win.AudioContext();
  context.resume();
  assert.equal(context.state, 'suspended');
  listeners.focus();
  assert.equal(context.state, 'running');
  session.start(); session.match(true);
  assert.equal(session.active, true);
});

test('watchShell ignores focus unless asked', () => {
  const {win, listeners} = focusWindow(false);
  const session = createSession();
  watchShell(win, session);
  session.start(); session.match(true);
  assert.equal(session.active, true);
  assert.equal(listeners.blur, undefined);
});
