// SPDX-License-Identifier: GPL-3.0-or-later
// GameDistribution (Azerion) portal adapter, injected only by
// platforms/web/package-portal.py after portal-core.js and before the
// GameDistribution SDK, which reads window.GD_OPTIONS when it loads. The packager
// sets GD_OPTIONS.gameId; this adds the event handler. SDK_GAME_PAUSE pauses the
// game for an ad or the portal's own overlay and SDK_GAME_START resumes it.
(() => {
  'use strict';

  function loadCore(win) {
    return typeof module === 'object' && module.exports ? require('./portal-core') : win.glob2PortalCore;
  }

  function create(win, core = loadCore(win)) {
    const audio = core.createAudio(win);
    const session = core.createSession();
    function onEvent(event) {
      if (event?.name === 'SDK_GAME_PAUSE') {
        audio.pause();
        session.hold(true);
      } else if (event?.name === 'SDK_GAME_START') {
        audio.resume();
        session.hold(false);
      }
    }
    const options = win.GD_OPTIONS || (win.GD_OPTIONS = {});
    const previous = options.onEvent;
    options.onEvent = event => {
      onEvent(event);
      if (typeof previous === 'function') previous(event);
    };
    return { audio, session, onEvent };
  }

  if (typeof module === 'object' && module.exports) module.exports = { create };
  else if (typeof window === 'object') {
    const adapter = create(window);
    window.glob2GameDistribution = adapter;
    window.glob2PortalCore.watchShell(window, adapter.session);
  }
})();
