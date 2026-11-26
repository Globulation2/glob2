const {test} = require('node:test');
const assert = require('node:assert/strict');
const {createAudio, createSession, watchShell} = require('../portal-core');
const {fakeWindow} = require('./portal-fixture');

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

test('gameplay follows loaded, visible and not held', () => {
  const events = [];
  const session = createSession({gameplay: active => events.push(active), loaded: () => events.push('loaded')});
  session.visible(true); session.hold(true);
  session.start();
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
  win.document.hidden = true; listeners.visibilitychange();
  assert.deepEqual(calls, [0, 40, 100, 'loaded', true, false]);
});
