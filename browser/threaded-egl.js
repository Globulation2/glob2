// SPDX-License-Identifier: GPL-3.0-or-later
// SDL uses EGL for WebGL contexts. The pinned SDK proxies EGL to the UI thread
// by default, but our canvas belongs to the application pthread. Keep the entire
// EGL state machine in that realm so creation, binding and teardown agree.
const glob2ThreadedEGL = {};
for (const name of [
  'eglGetDisplay', 'eglInitialize', 'eglTerminate', 'eglGetConfigs',
  'eglChooseConfig', 'eglGetConfigAttrib', 'eglCreateWindowSurface',
  'eglDestroySurface', 'eglCreateContext', 'eglDestroyContext',
  'eglQuerySurface', 'eglQueryContext', 'eglGetError', 'eglQueryString',
  'eglBindAPI', 'eglQueryAPI', 'eglWaitClient', 'eglWaitNative',
  'eglSwapInterval', 'eglMakeCurrent', 'eglGetCurrentContext',
  'eglGetCurrentSurface', 'eglGetCurrentDisplay', 'eglSwapBuffers',
  'eglReleaseThread',
]) glob2ThreadedEGL[name + '__proxy'] = 'none';

// SDL's asynchronous resize callback can run while the application is suspended
// for context loss. The pinned SDK indexes getParameter(GL_VIEWPORT) even when
// that query returns null. Let it update canvas/shared dimensions without GPU
// queries; the application restores graphics and applies the retained viewport.
glob2ThreadedEGL.$setCanvasElementSizeCallingThread__postset = `
  var glob2OriginalCanvasResize = setCanvasElementSizeCallingThread;
  setCanvasElementSizeCallingThread = (target, width, height) => {
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
addToLibrary(glob2ThreadedEGL);
