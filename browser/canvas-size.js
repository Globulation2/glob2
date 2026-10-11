// SPDX-License-Identifier: GPL-3.0-or-later
// SDL's asynchronous resize callback can run while the application is suspended
// for context loss. The pinned SDK indexes getParameter(GL_VIEWPORT) even when
// that query returns null. Let it update canvas/shared dimensions without GPU
// queries; the application restores graphics and applies the retained viewport.
const glob2CanvasResizePostset = `
  var glob2OriginalCanvasResize = GLOB2_CANVAS_RESIZE;
  GLOB2_CANVAS_RESIZE = (target, width, height) => {
    var targetCanvas = findCanvasEventTarget(target);
    var canvas = targetCanvas?.offscreenCanvas || targetCanvas;
    // Assigning identical DOM canvas dimensions clears its pixels. SDL can
    // repeat these requests after presentation; keep software frames visible.
    // Transferred/GPU canvases still need the SDK's viewport and owner handling.
    if (canvas && !targetCanvas.offscreenCanvas && !targetCanvas.controlTransferredOffscreen &&
        !canvas.GLctxObject && canvas.width === width && canvas.height === height &&
        (!canvas.canvasSharedPtr ||
         (HEAP32[canvas.canvasSharedPtr >> 2] === width &&
          HEAP32[(canvas.canvasSharedPtr + 4) >> 2] === height))) return 0;
    var context = canvas?.GLctxObject;
    if (!context?.GLctx?.isContextLost())
      return glob2OriginalCanvasResize(target, width, height);
    canvas.GLctxObject = null;
    try { return glob2OriginalCanvasResize(target, width, height); }
    finally { canvas.GLctxObject = context; }
  };
`;
// Serial SDK builds write DOM dimensions directly; pthread builds use the
// calling-thread helper so offscreen ownership and proxying remain intact.
#if PTHREADS
addToLibrary({$setCanvasElementSizeCallingThread__postset:
  glob2CanvasResizePostset.replaceAll('GLOB2_CANVAS_RESIZE', 'setCanvasElementSizeCallingThread')});
#else
addToLibrary({emscripten_set_canvas_element_size__postset:
  glob2CanvasResizePostset.replaceAll('GLOB2_CANVAS_RESIZE', '_emscripten_set_canvas_element_size')});
#endif

// Called only after SDL has retired its previous window/context. Messages have
// no `cmd` member so the pinned pthread command dispatcher ignores them.
addToLibrary({
  $Glob2SurfaceBridge__deps: ['$GL', 'malloc', 'free'
#if PTHREADS
    , 'pthread_self'
#endif
  ],
  $Glob2SurfaceBridge: {
    state: 1, request: 0, pending: null, failure: '', listening: false,
    prepare(gpu, width, height) {
      if (this.pending) return -1;
      this.failure = '';
#if PTHREADS
      if (ENVIRONMENT_IS_PTHREAD) {
        if (!this.listening) {
          this.listening = true;
          addEventListener('message', event => {
            var reply = event.data?.glob2Surface;
            if (!reply || reply.request !== this.request || !this.pending) return;
            if (reply.action === 'failed') {
              this.failure = reply.error;
              if (this.pending.sharedPtr) _free(this.pending.sharedPtr);
              this.pending = null;
              this.state = -1;
            } else if (reply.action === 'ready') {
              var next = reply.offscreenCanvas;
              if (next) {
                next.id = reply.id;
                next.canvasSharedPtr = reply.canvasSharedPtr;
                GL.offscreenCanvases[reply.id] = {offscreenCanvas:next,canvasSharedPtr:reply.canvasSharedPtr,id:reply.id};
              } else delete GL.offscreenCanvases.canvas;
              Module['canvas'] = next || null;
              Module.contextLoss = null;
              this.state = 1;
            }
          });
        }
        var sharedPtr = gpu ? _malloc(12) : 0;
        if (gpu && !sharedPtr) { this.state = -1; this.failure = 'Surface allocation failed'; return -1; }
        if (sharedPtr) {
          HEAP32[sharedPtr >> 2] = width;
          HEAP32[(sharedPtr + 4) >> 2] = height;
          HEAP32[(sharedPtr + 8) >> 2] = _pthread_self();
        }
        this.pending = {canvas:Module['canvas'], previous:GL.offscreenCanvases.canvas, sharedPtr};
        this.state = 0;
        postMessage({glob2Surface:{action:'prepare',request:++this.request,gpu:!!gpu,width,height,sharedPtr}});
        return 0;
      }
#endif
      try {
        Module.glob2PresentationSurface.prepare({gpu:!!gpu,width,height});
        this.pending = {};
        return this.state = 1;
      } catch (error) { Module.glob2PresentationSurface?.releaseRetirement(); this.failure = String(error); return this.state = -1; }
    },
    finish(success) {
      if (!this.pending || this.state !== 1) return;
#if PTHREADS
      if (ENVIRONMENT_IS_PTHREAD) {
        if (success) {
          if (this.pending.previous?.canvasSharedPtr) _free(this.pending.previous.canvasSharedPtr);
        } else {
          if (this.pending.previous) GL.offscreenCanvases.canvas = this.pending.previous;
          else delete GL.offscreenCanvases.canvas;
          Module['canvas'] = this.pending.canvas;
          if (this.pending.sharedPtr) _free(this.pending.sharedPtr);
        }
        postMessage({glob2Surface:{action:'finish',request:this.request,success:!!success}});
      } else
#endif
      Module.glob2PresentationSurface.finish(!!success);
      this.pending = null;
    }
  },
  glob2_surface_prepare__deps: ['$Glob2SurfaceBridge'],
  glob2_surface_prepare: (gpu,width,height) => Glob2SurfaceBridge.prepare(gpu,width,height),
  glob2_surface_poll__deps: ['$Glob2SurfaceBridge'],
  glob2_surface_poll: () => Glob2SurfaceBridge.state,
  // Query the calling presentation realm, which may be the application worker.
  glob2_surface_webgpu_available: () => +Boolean(globalThis.navigator?.gpu),
  glob2_surface_startup_uses_gl: () => +(Module.renderer === 'webgl2'),
  glob2_gl_override_texture_units__deps: ['$GL'],
  glob2_gl_override_texture_units: requested => {
    const previous = Module['GL_MAX_TEXTURE_IMAGE_UNITS'];
    Module['GL_MAX_TEXTURE_IMAGE_UNITS'] = Math.min(requested, GLctx.getParameter(GLctx.MAX_TEXTURE_IMAGE_UNITS));
    return previous === undefined ? -1 : previous;
  },
  glob2_gl_restore_texture_units: previous => {
    if (previous < 0) delete Module['GL_MAX_TEXTURE_IMAGE_UNITS'];
    else Module['GL_MAX_TEXTURE_IMAGE_UNITS'] = previous;
  },
  glob2_gl_copy_readback__deps: ['$GL'],
  glob2_gl_copy_readback: (destination, bytes) => {
    // WebGL may reject shared Wasm views; copy via an ordinary ArrayBuffer.
    const data = new Uint8Array(bytes);
    GLctx.getBufferSubData(0x88EB, 0, data);
    HEAPU8.set(data, destination);
  },
  glob2_surface_finish__deps: ['$Glob2SurfaceBridge'],
  glob2_surface_finish: success => Glob2SurfaceBridge.finish(success),
  glob2_surface_error__deps: ['$Glob2SurfaceBridge','$stringToUTF8'],
  glob2_surface_error: (destination,size) => stringToUTF8(Glob2SurfaceBridge.failure,destination,size)
});

// SDL_DestroyWindow leaves fullscreen before retiring its canvas. The
// coordinator calls retire before teardown; preserve the stable DOM ancestor
// only for that transaction. Ordinary fullscreen exit retains SDK behavior.
addToLibrary({
  glob2_surface_retire__proxy: 'sync',
  glob2_surface_retire__deps: ['$currentFullscreenStrategy'],
  glob2_surface_retire: () => {
    Module.glob2PresentationSurface?.retire();
    // SDL's resize callback userdata belongs to the retiring SDL_WindowData.
    // Browser presentation metrics continue to drive the replacement window.
    if (typeof currentFullscreenStrategy !== 'undefined' && currentFullscreenStrategy) {
      currentFullscreenStrategy.canvasResizedCallback = 0;
      currentFullscreenStrategy.canvasResizedCallbackUserData = 0;
    }
  },
  emscripten_exit_fullscreen__postset: `
    var glob2OriginalExitFullscreen = _emscripten_exit_fullscreen;
    _emscripten_exit_fullscreen = () => {
      if (typeof document === 'undefined') return glob2OriginalExitFullscreen();
      var surface = Module.glob2PresentationSurface;
      var active = document.fullscreenElement || document.webkitFullscreenElement || document.mozFullScreenElement;
      if (surface?.retiring && active && active === surface.fullscreenHost) return 0;
      return glob2OriginalExitFullscreen();
    };
  `,
  $fillFullscreenChangeEventData__postset: `
    var glob2OriginalFullscreenData = fillFullscreenChangeEventData;
    fillFullscreenChangeEventData = pointer => {
      glob2OriginalFullscreenData(pointer);
      var surface = Module.glob2PresentationSurface;
      var active = document.fullscreenElement || document.webkitFullscreenElement || document.mozFullScreenElement || JSEvents.previousFullscreenElement;
      if (surface?.fullscreenHost && active === surface.fullscreenHost) {
        stringToUTF8('CANVAS', pointer + {{{ C_STRUCTS.EmscriptenFullscreenChangeEvent.nodeName }}}, {{{ cDefs.EM_HTML5_LONG_STRING_LEN_BYTES }}});
        stringToUTF8(surface.canvas.id, pointer + {{{ C_STRUCTS.EmscriptenFullscreenChangeEvent.id }}}, {{{ cDefs.EM_HTML5_LONG_STRING_LEN_BYTES }}});
      }
    };
  `,
  $registerRestoreOldStyle__postset: `
    var glob2OriginalFullscreenRestore = registerRestoreOldStyle;
    registerRestoreOldStyle = canvas => {
      var surface = Module.glob2PresentationSurface;
      if (!surface?.fullscreenHost || canvas !== surface.canvas) return glob2OriginalFullscreenRestore(canvas);
      // The SDK closure captures a concrete canvas and its shared pointer.
      // Resolve the current canvas at exit so replacement never uses freed data.
      return surface.fullscreenRestore(getCanvasElementSize, setCanvasElementSize);
    };
  `
});
