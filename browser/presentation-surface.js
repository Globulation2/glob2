// SPDX-License-Identifier: GPL-3.0-or-later
// DOM ownership only. The js-library bridge owns Emscripten's per-realm maps.
(function(root) {
  function create(module, options = {}) {
    const document = options.document || root.document;
    const bindings = new Set();
    let canvas = module.canvas, transaction = null, generation = 0;
    const fullscreenHost = options.fullscreenHost || document.getElementById?.('presentation-host');
    let retiring = false, removeFullscreenRestore = null;
    function routeFullscreen(target) {
      if (!fullscreenHost) return;
      for (const name of ['requestFullscreen', 'webkitRequestFullscreen', 'mozRequestFullScreen', 'mozRequestFullscreen'])
        if (typeof fullscreenHost[name] === 'function') target[name] = (...args) => fullscreenHost[name](...args);
    }
    routeFullscreen(canvas);
    function publish(next) {
      for (const binding of bindings) binding.cleanup?.();
      canvas = next;
      module.canvas = next;
      routeFullscreen(next);
      module.ctx = null;
      module.contextLoss = null;
      if (module.SDL3) {
        for (const key of ['ctx', 'ctxCanvas', 'image', 'imageCtx', 'data32', 'data32Data'])
          module.SDL3[key] = null;
      }
      for (const binding of bindings) binding.cleanup = binding.install(canvas);
      module.gpuLost = false;
      module.gpuRestorePending = false;
      module.visibilityPending = true;
    }
    function restore() {
      const current = transaction;
      if (!current) return;
      canvas.replaceWith(current.previous);
      publish(current.previous);
      if (current.focused) canvas.focus({preventScroll:true});
      transaction = null;
      retiring = false;
    }
    return {
      get canvas() { return canvas; },
      get generation() { return generation; },
      get fullscreenHost() { return fullscreenHost; },
      get retiring() { return retiring; },
      retire() { retiring = true; },
      releaseRetirement() { retiring = false; },
      fullscreenRestore(getSize, setSize) {
        removeFullscreenRestore?.();
        const size = getSize(canvas), style = canvas.style.cssText;
        const restore = () => {
          if (document.fullscreenElement || document.webkitFullscreenElement || document.mozFullScreenElement) return;
          removeFullscreenRestore();
          setSize(canvas, size[0], size[1]);
          canvas.style.cssText = style;
        };
        const events = ['fullscreenchange', 'webkitfullscreenchange', 'mozfullscreenchange'];
        for (const event of events) document.addEventListener(event, restore);
        removeFullscreenRestore = () => {
          for (const event of events) document.removeEventListener(event, restore);
          removeFullscreenRestore = null;
        };
        return restore;
      },
      bind(install) {
        const binding = {install, cleanup:install(canvas)};
        bindings.add(binding);
        return () => { binding.cleanup?.(); bindings.delete(binding); };
      },
      prepare({gpu, width, height, sharedPtr = 0}) {
        if (transaction) throw new Error('A presentation surface transition is already active');
        if (!Number.isInteger(width) || !Number.isInteger(height) || width < 1 || height < 1)
          throw new Error('Invalid presentation dimensions');
        // A fullscreen canvas cannot be replaced without exiting fullscreen,
        // and re-entry needs a fresh trusted gesture. Keep the old surface.
        if (document.fullscreenElement === canvas)
          throw new Error('Exit browser fullscreen before changing renderer');
        const previous = canvas, focused = document.activeElement === previous;
        const next = previous.cloneNode(false);
        next.width = width; next.height = height;
        // cloneNode copies DOM attributes, never context or SDK expando state.
        let offscreenCanvas = null;
        if (gpu && module.executionMode === 'threaded') {
          if (!next.transferControlToOffscreen || !sharedPtr)
            throw new Error('Worker presentation surface transfer unavailable');
          offscreenCanvas = next.transferControlToOffscreen();
          next.controlTransferredOffscreen = true;
          next.canvasSharedPtr = sharedPtr;
        }
        transaction = {previous, next, focused, gpu};
        try {
          if (document.pointerLockElement === previous) document.exitPointerLock?.();
          previous.replaceWith(next);
          publish(next);
          if (focused) next.focus({preventScroll:true});
          return {offscreenCanvas, canvasSharedPtr:sharedPtr, id:next.id, generation:generation + 1};
        } catch (error) { restore(); throw error; }
      },
      finish(success) {
        if (!transaction) throw new Error('No presentation surface transition is active');
        if (!success) { restore(); return; }
        module.renderer = transaction.gpu ? 'webgl2' : 'software';
        transaction = null;
        retiring = false;
        generation++;
      },
      attachWorker(worker) {
        if (worker.glob2SurfaceAttached) return;
        worker.glob2SurfaceAttached = true;
        worker.addEventListener('message', event => {
          const message = event.data?.glob2Surface;
          if (!message) return;
          try {
            if (message.action === 'prepare') {
              const surface = this.prepare(message);
              worker.postMessage({glob2Surface:{action:'ready', request:message.request, ...surface}},
                surface.offscreenCanvas ? [surface.offscreenCanvas] : []);
            } else if (message.action === 'finish') {
              this.finish(message.success);
            }
          } catch (error) {
            // Transfer can fail after DOM publication; restore before replying.
            if (message.action === 'prepare') {
              if (transaction) restore();
              retiring = false;
            }
            worker.postMessage({glob2Surface:{action:'failed',request:message.request,error:String(error)}});
          }
        });
      }
    };
  }
  root.Glob2PresentationSurface = {create};
  if (typeof module !== 'undefined' && module.exports) module.exports = root.Glob2PresentationSurface;
})(globalThis);
