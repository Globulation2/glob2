// SPDX-License-Identifier: GPL-3.0-or-later
// GamePix portal adapter, injected only by platforms/web/package-portal.py after
// the GamePix SDK and portal-core.js and before every game script. GamePix requires
// pause/resume handlers that stop gameplay, input and audio; they must be
// idempotent, so the pause is a hold and a silence reason (flags, not counters,
// portal-core.js), as for GameDistribution. lifecycle.loaded() marks the
// interactive main menu and lifecycle.gameStart() the first real gameplay, the
// first match ("when real gameplay starts", partners.gamepix.com/sdk/doc/javascript).
// Ad breaks: GamePix.ads.interstitial() at the natural pauses the game reports
// (portal-core.js createBreaks), never before the first match and at most one
// break every three minutes, as for every ad portal. The SDK calls
// the pause and resume handlers around the ad itself.
(() => {
  'use strict';

  function loadCore(win) {
    return typeof module === 'object' && module.exports ? require('./portal-core') : win.glob2PortalCore;
  }

  function create(win, core = loadCore(win)) {
    const audio = core.createAudio(win);
    let started = false;
    const session = core.createSession({
      progress: value => win.GamePix?.lifecycle?.loading?.(value),
      loaded: () => win.GamePix?.lifecycle?.loaded?.(),
      gameplay: active => {
        if (!active || started) return;
        started = true;
        win.GamePix?.lifecycle?.gameStart?.();
      },
    });
    const breaks = core.createBreaks(win, {
      session, audio,
      show: () => {
        const ads = win.GamePix?.ads;
        if (typeof ads?.interstitial === 'function') return ads.interstitial();
        if (typeof win.GamePix?.interstitialAd === 'function') return win.GamePix.interstitialAd();
        return null;
      },
    });
    core.installHost(win, breaks);
    // Hold and silence together; both are flags, so repeats do nothing.
    const pause = () => { audio.pause(); session.hold(true); };
    const resume = () => { audio.resume(); session.hold(false); };
    return {
      pause,
      resume,
      progress: session.progress,
      start: session.start,
      get paused() { return audio.paused; },
      get started() { return started; },
      contexts: audio.contexts,
      session,
      breaks,
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
