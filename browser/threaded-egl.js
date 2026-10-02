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
addToLibrary(glob2ThreadedEGL);
