const {test} = require('node:test');
const assert = require('node:assert/strict');
const {create} = require('../gamepix');

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

test('loading reports increasing whole percentages, then loaded and gameStart once', () => {
  const {calls, adapter} = fixture();
  adapter.progress('12.7'); adapter.progress(12); adapter.progress(5); adapter.progress(140);
  adapter.start(); adapter.start(); adapter.progress(50);
  assert.deepEqual(calls, [['loading', 12], ['loading', 100], ['loaded'], ['gameStart']]);
});

test('the adapter works without the SDK', () => {
  const win = {document: {hidden: false}};
  const adapter = create(win);
  adapter.progress(10); adapter.start(); adapter.pause(); adapter.resume();
});
