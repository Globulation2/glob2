// SPDX-License-Identifier: GPL-3.0-or-later
// GameDistribution (Azerion) portal adapter, injected only by
// platforms/web/package-portal.py after portal-core.js and before the
// GameDistribution SDK, which reads window.GD_OPTIONS when it loads. The packager
// sets GD_OPTIONS.gameId; this adds the event handler. SDK_GAME_PAUSE pauses the
// game for an ad or the portal's own overlay and SDK_GAME_START resumes it.
// Ad breaks: gdsdk.showAd() (an interstitial) at the natural pauses the game
// reports (portal-core.js createBreaks), including the pre-roll GameDistribution
// requires when the player starts the first match; the SDK sets the interval.
(() => {
  'use strict';

  function loadCore(win) {
    return typeof module === 'object' && module.exports ? require('./portal-core') : win.glob2PortalCore;
  }

  function create(win, core = loadCore(win)) {
    const audio = core.createAudio(win);
    const session = core.createSession();
    const breaks = core.createBreaks(win, {
      session, audio, firstStart: true,
      show: () => typeof win.gdsdk?.showAd === 'function' ? win.gdsdk.showAd() : null,
    });
    core.installHost(win, breaks);
    function onEvent(event) {
      if (event?.name === 'SDK_GAME_PAUSE') {
        audio.pause();
        session.hold(true);
        breaks.started();
      } else if (event?.name === 'SDK_GAME_START') {
        audio.resume();
        session.hold(false);
        breaks.finished();
      }
    }
    const options = win.GD_OPTIONS || (win.GD_OPTIONS = {});
    const previous = options.onEvent;
    options.onEvent = event => {
      onEvent(event);
      if (typeof previous === 'function') previous(event);
    };
    return { audio, session, breaks, onEvent };
  }

  if (typeof module === 'object' && module.exports) module.exports = { create };
  else if (typeof window === 'object') {
    const adapter = create(window);
    window.glob2GameDistribution = adapter;
    window.glob2PortalCore.watchShell(window, adapter.session);
  }
})();
