const {test} = require('node:test');
const assert = require('node:assert/strict');
const {threadingSupport,watchThreadStartup} = require('../loader.js');
function scope(overrides={}) {
  return {
    crossOriginIsolated:true, SharedArrayBuffer, WebAssembly, Blob,
    URL:{createObjectURL:()=> 'blob:test', revokeObjectURL(){}},
    Worker:class {postMessage(){queueMicrotask(()=>this.onmessage({data:true}));} terminate(){}},
    ...overrides,
  };
}
test('threaded software mode requires a successful shared-memory worker',async()=>{
  assert.equal(await threadingSupport('software',scope()),null);
});
test('serial fallback explains missing isolation, memory and worker support',async()=>{
  assert.match(await threadingSupport('software',scope({crossOriginIsolated:false})),/isolation/);
  assert.match(await threadingSupport('software',scope({SharedArrayBuffer:undefined})),/shared memory/);
  assert.match(await threadingSupport('software',scope({Worker:undefined})),/workers/);
  assert.match(await threadingSupport('software',scope({Worker:class {constructor(){throw Error('CSP');}}})),/rejected/);
});
test('worker WebGL has an additional canvas-transfer requirement',async()=>{
  assert.match(await threadingSupport('webgl2',scope()),/WebGL/);
  assert.equal(await threadingSupport('webgl2',scope({HTMLCanvasElement:{prototype:{transferControlToOffscreen(){}}},OffscreenCanvas:class {}})),null);
});

function startupScope(Worker = class extends EventTarget {}) {
  let timeout;
  return {Worker, setTimeout(callback) { timeout = callback; return 1; },
    clearTimeout() { timeout = null; }, expire() { timeout?.(); }};
}
test('actual pthread errors and silent startup stalls are observed, then detached', () => {
  const scope = startupScope();
  const NativeWorker = scope.Worker;
  const failures = [];
  const stop = watchThreadStartup(reason => failures.push(reason), scope);
  const worker = new scope.Worker('threaded/index.js');
  assert.ok(worker instanceof NativeWorker);
  worker.onerror = () => {}; // SDK handler assignment cannot replace our listener
  const error = new Event('error');
  error.message = 'Unable to instantiate worker runtime';
  worker.dispatchEvent(error);
  assert.match(failures[0], /Unable to instantiate/);
  scope.expire();
  assert.match(failures[1], /timed out/);
  stop(); stop();
  assert.equal(scope.Worker, NativeWorker);
  worker.dispatchEvent(error); scope.expire();
  assert.equal(failures.length, 2);
});
test('synchronous pthread creation errors reach fallback and retain their exception', () => {
  const error = new Error('Worker denied');
  const scope = startupScope(class {constructor() {throw error;}});
  let failure;
  const stop = watchThreadStartup(reason => {failure = reason;}, scope);
  assert.throws(() => new scope.Worker('threaded/index.js'), value => value === error);
  assert.match(failure, /Worker denied/);
  stop();
});


test('persistence snapshots survive worker rename and byte changes during the database scan', () => {
  const Storage = require('../storage');
  let remoteReady, committed;
  const bytes = new Uint8Array([1, 2, 3]);
  const idbfs = {
    syncfs() { throw new Error('Unexpected restore'); },
    getLocalSet(mount, done) { done(null, {type:'local', entries:{'/save':{timestamp:1}}}); },
    loadLocalEntry(path, done) { done(null, {contents:bytes, mode:1, timestamp:1}); },
    getRemoteSet(mount, done) { remoteReady=done; },
    reconcile(local, remote, done) { this.loadLocalEntry('/save', (error, entry) => {
      committed=entry;done(error);
    }); },
  };
  Storage.installSnapshotPersistence(idbfs);
  let finished=false;
  idbfs.syncfs({}, false, error => { assert.equal(error, null); finished=true; });
  bytes[0]=9;
  idbfs.loadLocalEntry = () => { throw new Error('Worker removed the original path'); };
  remoteReady(null, {type:'remote'});
  assert.deepEqual([...committed.contents], [1, 2, 3]);
  assert.equal(finished, true);
  assert.throws(() => idbfs.loadLocalEntry('/save'), /removed/);
});
