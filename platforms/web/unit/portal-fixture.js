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

// Let pending promise callbacks run.
const settle = () => new Promise(resolve => setImmediate(resolve));

module.exports = {FakeContext, fakeWindow, settle};
