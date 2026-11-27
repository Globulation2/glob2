// Browser crash capture (shell.html glob2CrashCapture): which errors count, the
// WebErrorReport it stores, and the build ID it names.
const assert = require('node:assert/strict');
const {readFileSync} = require('node:fs');
const {createHash, webcrypto} = require('node:crypto');
const path = require('node:path');
const vm = require('node:vm');
const {test} = require('node:test');
const shell = readFileSync(path.join(__dirname, '../shell.html'), 'utf8');
const start = shell.indexOf('const glob2CrashCapture =');
const end = shell.indexOf('// End crash capture.', start);

function load() {
  const context = vm.createContext({WebAssembly, crypto: webcrypto, Uint8Array, Array, String, JSON});
  vm.runInContext(shell.slice(start, end) + ';globalThis.capture = glob2CrashCapture;', context);
  return context.capture;
}

function host(started = true) {
  const files = {};
  const wasm = Buffer.from('fake wasm module bytes');
  const host = {
    Module: {glob2ApplicationStarted: started, glob2WasmFile: 'threaded/index.wasm', storage: {changes: 0, changed() { this.changes++; }}},
    FS: {mkdirTree() {}, writeFile(name, text) { files[name] = text; }},
    fetch: async url => ({arrayBuffer: async () => (url === 'threaded/index.wasm' ? wasm : Buffer.alloc(0))}),
    crypto: webcrypto,
  };
  return {host, files, id: createHash('sha256').update(wasm).digest('hex').slice(0, 32)};
}

test('only WebAssembly traps and wasm stacks are the game crashing', () => {
  const capture = load();
  assert.equal(capture.fatal(new WebAssembly.RuntimeError('unreachable')), true);
  assert.equal(capture.fatal({stack: 'Error\n    at wasm-function[4127]:0x1a2b'}), true);
  assert.equal(capture.fatal(new Error('extension failed')), false);
  assert.equal(capture.fatal(undefined), false);
});

test('a crash is stored once as a WebErrorReport naming the build', async () => {
  const capture = load();
  const {host: page, files, id} = host();
  const error = new WebAssembly.RuntimeError('memory access out of bounds');
  assert.equal(await capture.capture('error', error, 'at wasm-function[88]:0x4f00', page), true);
  assert.equal(await capture.capture('error', error, '', page), false);
  const names = Object.keys(files);
  assert.equal(names.length, 1);
  assert.match(names[0], /^\/home\/web_user\/\.glob2\/reports\/crashes\/[0-9a-f]{32}\.web\.json$/);
  assert.deepEqual(JSON.parse(files[names[0]]), {
    schema: 1, source: 'error', message: 'memory access out of bounds',
    stack: 'at wasm-function[88]:0x4f00', buildId: id,
  });
  assert.equal(page.Module.storage.changes, 1);
});

test('nothing is stored before the game started', async () => {
  const capture = load();
  const {host: page, files} = host(false);
  assert.equal(await capture.capture('abort', 'Unable to load game runtime', '', page), false);
  assert.deepEqual(files, {});
});

test('long messages and stacks are cut to the protocol limits', () => {
  const capture = load();
  const entry = capture.report('abort', 'x'.repeat(5000), 'y'.repeat(40000));
  assert.equal(entry.message.length, 1024);
  assert.equal(entry.stack.length, 32768);
});
