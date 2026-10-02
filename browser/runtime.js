// SPDX-License-Identifier: GPL-3.0-or-later
// Keep fixed-function emulation bounded in every JS realm, including render workers.
Module['GL_MAX_TEXTURE_IMAGE_UNITS'] = 1;
// A completion promise works for hosted games and synchronous command-line tools.
// pthreads have their own JS realms; only the UI/runtime realm owns these hooks.
if (typeof ENVIRONMENT_IS_PTHREAD === 'undefined' || !ENVIRONMENT_IS_PTHREAD) {
  Module.start = args => {
    if (Module.glob2StartCalled) return Promise.reject(new Error('Application already started'));
    Module.glob2StartCalled = true;
    return new Promise((resolve, reject) => {
      let completed = false;
      Module.glob2Complete = code => {
        if (completed) return;
        completed = true;
        Module.onGameExit?.(code);
        resolve(code);
      };
      Module.glob2LaunchFailed = error => {
        Module.onLaunchFailed?.(error);
        reject(new Error('Application worker creation failed: ' + error));
      };
      const abort = Module.onAbort;
      Module.onAbort = reason => { abort?.(reason); reject(new Error(String(reason))); };
      try { Module.callMain(args); } catch (error) { reject(error); }
    });
  };
}
