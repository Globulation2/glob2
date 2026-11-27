// Shared stubs for the portal adapter tests: a fake Web Audio context and a
// minimal window. Not a test file itself (unit/*.test.js only).
class FakeContext {
  constructor() { this.state = 'running'; this.listeners = []; }
  addEventListener(name, listener) { if (name === 'statechange') this.listeners.push(listener); }
  set(state) { this.state = state; this.listeners.forEach(listener => listener()); }
  suspend() { this.set('suspended'); return Promise.resolve(); }
  resume() { this.set('running'); return Promise.resolve(); }
  close() { this.state = 'closed'; return Promise.resolve(); }
}

function fakeWindow(extra = {}) {
  return {AudioContext: FakeContext, document: {hidden: false}, console: {warn() {}}, ...extra};
}

// Manual timers for the ad-break timeouts: run() fires every due callback.
function fakeTimers() {
  let clock = 0, next = 1;
  const pending = new Map();
  return {
    setTimeout: (callback, delay) => { pending.set(next, {callback, at: clock + delay}); return next++; },
    clearTimeout: id => pending.delete(id),
    advance(ms) {
      clock += ms;
      for (const [id, timer] of [...pending]) if (timer.at <= clock) { pending.delete(id); timer.callback(); }
    },
    get pending() { return pending.size; },
    get now() { return clock; },
  };
}

// A window whose portal policy carries ads, as package-portal.py writes it.
function adWindow(name, extra = {}) {
  const timers = fakeTimers();
  return fakeWindow({GLOB2_PORTAL: {name, externalLinks: false, onlineSignIn: false, ads: true},
                     setTimeout: timers.setTimeout, clearTimeout: timers.clearTimeout, timers,
                     performance: {now: () => timers.now}, ...extra});
}

// A promise with its resolve function, standing in for an ad that is playing.
function deferred() {
  let resolve;
  const promise = new Promise(done => { resolve = done; });
  return {promise, resolve};
}

// Let pending promise callbacks run.
const settle = () => new Promise(resolve => setImmediate(resolve));

module.exports = {FakeContext, fakeWindow, fakeTimers, adWindow, deferred, settle};
