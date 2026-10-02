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
  async function load() {
    const module = root.Module;
    const forced = new URLSearchParams(location.search).get('threads');
    const reason = forced === 'serial' ? (new URLSearchParams(location.search).get('thread-fallback') || 'serial requested') : await threadingSupport(module.renderer);
    module.executionMode = reason ? 'serial' : 'threaded';
    module.threadFallback = reason;
    const fallback = reason => {
      if (module.executionMode !== 'threaded' || module.glob2ApplicationStarted) return false;
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
    const prefix = reason ? '' : 'threaded/';
    module.locateFile = name => name.endsWith('.data') ? name : prefix + name;
    const script = document.createElement('script');
    script.src = prefix + 'index.js';
    script.onerror = () => module.onAbort?.('Unable to load game runtime');
    document.body.append(script);
  }
  root.Glob2BrowserLoader = {threadingSupport, load};
  if (typeof module !== 'undefined' && module.exports) module.exports = root.Glob2BrowserLoader;
  else load().catch(error => root.Module.onAbort?.(String(error)));
})(globalThis);
