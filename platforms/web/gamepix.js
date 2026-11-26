// SPDX-License-Identifier: GPL-3.0-or-later
// GamePix portal adapter, injected only by platforms/web/package-portal.py after
// the GamePix SDK and portal-core.js and before every game script. GamePix requires
// pause/resume handlers that mute audio; they must be idempotent, so paused state is
// a flag rather than a counter (portal-core.js).
(() => {
  'use strict';

  function loadCore(win) {
    return typeof module === 'object' && module.exports ? require('./portal-core') : win.glob2PortalCore;
  }

  function create(win, core = loadCore(win)) {
    const audio = core.createAudio(win);
    const session = core.createSession({
      progress: value => win.GamePix?.lifecycle?.loading?.(value),
      loaded: () => {
        win.GamePix?.lifecycle?.loaded?.();
        win.GamePix?.lifecycle?.gameStart?.();
      },
    });
    return {
      pause: audio.pause,
      resume: audio.resume,
      progress: session.progress,
      start: session.start,
      get paused() { return audio.paused; },
      contexts: audio.contexts,
      session,
    };
  }

  function attach(win, adapter, core = loadCore(win)) {
    win.GamePix?.events?.on?.('pause', adapter.pause);
    win.GamePix?.events?.on?.('resume', adapter.resume);
    core.watchShell(win, adapter.session);
  }

  if (typeof module === 'object' && module.exports) module.exports = { create, attach };
  else if (typeof window === 'object') {
    const adapter = create(window);
    window.glob2GamePix = adapter;
    attach(window, adapter);
  }
})();
