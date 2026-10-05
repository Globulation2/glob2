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
