// SPDX-License-Identifier: GPL-3.0-or-later
(function(root) {
  async function threadingSupport(renderer, scope = root) {
    if (!scope.crossOriginIsolated) return 'cross-origin isolation unavailable';
    if (typeof scope.SharedArrayBuffer !== 'function') return 'shared memory unavailable';
    if (typeof scope.Worker !== 'function') return 'workers unavailable';
    try { new scope.WebAssembly.Memory({initial:1, maximum:1, shared:true}); }
    catch (_) { return 'shared WebAssembly memory unavailable'; }
    if (renderer === 'webgl2' && (!scope.HTMLCanvasElement?.prototype.transferControlToOffscreen || !scope.OffscreenCanvas))
      return 'worker WebGL rendering unavailable';
    // Exercise a real worker and shared-memory operation before loading a runtime.
    const url = scope.URL.createObjectURL(new scope.Blob([
      'onmessage=e=>{try{Atomics.add(new Int32Array(e.data.memory),0,1);postMessage(!e.data.webgl || !!new OffscreenCanvas(1,1).getContext("webgl2"))}catch(_){postMessage(false)}}'
    ], {type:'text/javascript'}));
    try {
      return await new Promise(resolve => {
        let worker;
        const finish = reason => { clearTimeout(timer); worker?.terminate(); resolve(reason); };
        const timer = setTimeout(() => finish('worker startup timed out'), 5000);
        try {
          worker = new scope.Worker(url);
          worker.onmessage = event => finish(event.data ? null : 'worker shared memory unavailable');
          worker.onerror = () => finish('worker startup rejected');
          worker.postMessage({memory:new scope.SharedArrayBuffer(4),webgl:renderer==='webgl2'});
        } catch (_) { finish('worker startup rejected'); }
      });
    } finally { scope.URL.revokeObjectURL(url); }
  }
  function watchThreadStartup(failed, scope = root) {
    const NativeWorker = scope.Worker;
    const workers = new Map();
    let stopped = false;
    // The pinned SDK only throws from worker.onerror; it neither rejects its
    // pool-loading promise nor invokes onAbort. Observe the actual workers.
    class StartupWorker extends NativeWorker {
      constructor(...args) {
        try { super(...args); }
        catch (error) { failed('pthread worker creation failed: ' + error); throw error; }
        const error = event => failed('pthread worker startup failed: ' + (event.message || 'worker error'));
        this.addEventListener('error', error);
        workers.set(this, error);
      }
    }
    scope.Worker = StartupWorker;
    const timer = scope.setTimeout(() => failed('threaded runtime startup timed out'), 120000);
    return () => {
      if (stopped) return;
      stopped = true;
      scope.clearTimeout(timer);
      if (scope.Worker === StartupWorker) scope.Worker = NativeWorker;
      for (const [worker, error] of workers) worker.removeEventListener('error', error);
      workers.clear();
    };
  }
  async function load() {
    const module = root.Module;
    const forced = new URLSearchParams(location.search).get('threads');
    const reason = forced === 'serial' ? (new URLSearchParams(location.search).get('thread-fallback') || 'serial requested') : await threadingSupport(module.renderer);
    module.executionMode = reason ? 'serial' : 'threaded';
    module.threadFallback = reason;
    let stopWatching = () => {};
    let fallingBack = false;
    const fallback = reason => {
      if (module.executionMode !== 'threaded' || module.glob2ApplicationStarted) return false;
      if (fallingBack) return true;
      fallingBack = true;
      stopWatching();
      const url = new URL(location.href);
      url.searchParams.set('threads', 'serial');
      url.searchParams.set('thread-fallback', reason);
      location.replace(url.href);
      return true;
    };
    module.onLaunchFailed = error => fallback('application worker creation failed: ' + error);
    const abort = module.onAbort;
    module.onAbort = error => {
      if (!fallback('threaded runtime initialization failed: ' + error)) abort?.(error);
    };
    if (!reason) {
      stopWatching = watchThreadStartup(fallback);
      const started = module.onApplicationStarted;
      module.onApplicationStarted = (...args) => { stopWatching(); started?.(...args); };
    }
    const prefix = reason ? '' : 'threaded/';
    module.locateFile = name => name.endsWith('.data') ? name : prefix + name;
    const script = document.createElement('script');
    script.src = module.glob2RuntimeFiles?.[module.executionMode] || prefix + 'index.js';
    script.onerror = () => module.onAbort?.('Unable to load game runtime');
    document.body.append(script);
  }
  root.Glob2BrowserLoader = {threadingSupport, watchThreadStartup, load};
  if (typeof module !== 'undefined' && module.exports) module.exports = root.Glob2BrowserLoader;
  else load().catch(error => root.Module.onAbort?.(String(error)));
})(globalThis);
