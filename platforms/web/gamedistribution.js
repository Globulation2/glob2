// SPDX-License-Identifier: GPL-3.0-or-later
// GameDistribution (Azerion) portal adapter, injected only by
// platforms/web/package-portal.py after portal-core.js and before the
// GameDistribution SDK, which reads window.GD_OPTIONS when it loads. The packager
// sets GD_OPTIONS.gameId; this adds the event handler. SDK_GAME_PAUSE pauses the
// game for an ad or the portal's own overlay and SDK_GAME_START resumes it.
// Ad breaks: gdsdk.showAd() (an interstitial) at the natural pauses the game
// reports (portal-core.js createBreaks), including the pre-roll GameDistribution
// requires when the player starts the first match; the SDK sets the interval.
// showAd() is never called before SDK_READY: a break requested earlier waits for
// it (within createBreaks' start timeout), and SDK_ERROR ends it without an ad.
(() => {
  'use strict';

  function loadCore(win) {
    return typeof module === 'object' && module.exports ? require('./portal-core') : win.glob2PortalCore;
  }

  function create(win, core = loadCore(win)) {
    const audio = core.createAudio(win);
    const session = core.createSession();
    // 'loading' until SDK_READY or SDK_ERROR; breaks requested meanwhile wait.
    let sdkState = 'loading';
    let waiting = [];
    const showAd = () => win.gdsdk.showAd();
    const flush = () => {
      const pending = waiting;
      waiting = [];
      // A break the start timeout already released does not ask any more.
      for (const {ad, resolve} of pending) {
        if (sdkState !== 'ready' || !ad.active) { resolve(false); continue; }
        try { resolve(showAd()); } catch (error) { resolve(Promise.reject(error)); }
      }
    };
    const breaks = core.createBreaks(win, {
      session, audio, firstStart: true,
      show: ad => {
        if (typeof win.gdsdk?.showAd !== 'function' || sdkState === 'failed') return null;
        if (sdkState === 'ready') return showAd();
        return new Promise(resolve => waiting.push({ad, resolve}));
      },
    });
    core.installHost(win, breaks);
    function onEvent(event) {
      if (event?.name === 'SDK_READY' || event?.name === 'SDK_ERROR') {
        if (sdkState === 'loading') sdkState = event.name === 'SDK_READY' ? 'ready' : 'failed';
        flush();
      } else if (event?.name === 'SDK_GAME_PAUSE') {
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
    return { audio, session, breaks, onEvent, get sdkState() { return sdkState; } };
  }

  if (typeof module === 'object' && module.exports) module.exports = { create };
  else if (typeof window === 'object') {
    const adapter = create(window);
    window.glob2GameDistribution = adapter;
    window.glob2PortalCore.watchShell(window, adapter.session);
  }
})();
