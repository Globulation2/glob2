// SPDX-License-Identifier: GPL-3.0-or-later
// Invoke directly from a user gesture, while browser activation is still valid.
async function glob2ActivateAudio(context, reportError) {
  if (!context || context.state === 'closed' || context.state === 'running') return;
  try {
    await context.resume();
  } catch (error) {
    // SDL can close the context while a gesture's resume promise is pending.
    // A later gesture may retry other failures; never leave a rejected promise
    // unhandled or claim that audio started when the browser refused it.
    if (context.state !== 'closed') reportError(error);
  }
}
if (typeof module !== 'undefined' && module.exports) module.exports = glob2ActivateAudio;
if (typeof globalThis !== 'undefined') globalThis.glob2ActivateAudio = glob2ActivateAudio;


// SDL's own gesture/silence callbacks also call resume() and ignore its promise.
// Observe only SDL-owned contexts. Callers still receive the original promise,
// while late shutdown rejections cannot escape as global unhandled rejections.
function glob2ObserveSDLAudio(sdl, reportError) {
  const observed = new WeakSet();
  let current = sdl.audioContext;
  function observe(context) {
    if (!context || observed.has(context)) return;
    observed.add(context);
    const resume = context.resume;
    context.resume = function(...args) {
      const pending = resume.apply(this, args);
      pending.catch(error => { if (context.state !== 'closed') reportError(error); });
      return pending;
    };
  }
  Object.defineProperty(sdl, 'audioContext', {
    configurable:true, enumerable:true,
    get:() => current,
    set(context) { current=context; observe(context); }
  });
  observe(current);
}
if (typeof module !== 'undefined' && module.exports) module.exports.observeSDL = glob2ObserveSDLAudio;
if (typeof Module !== 'undefined') {
  Module.preRun = Module.preRun || [];
  Module.preRun.push(function() {
    Module.SDL2 = Module.SDL2 || {};
    glob2ObserveSDLAudio(Module.SDL2, error => Module.printErr('Audio activation failed: ' + error));
  });
}
