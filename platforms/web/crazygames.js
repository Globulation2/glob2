// SPDX-License-Identifier: GPL-3.0-or-later
// CrazyGames portal adapter (HTML5 SDK v3), injected only by
// platforms/web/package-portal.py after the SDK and portal-core.js and before every
// game script. The SDK must finish init() before any other call, so the loading
// and gameplay state the shell reached meanwhile is replayed once it resolves.
(() => {
  'use strict';

  function loadCore(win) {
    return typeof module === 'object' && module.exports ? require('./portal-core') : win.glob2PortalCore;
  }

  function create(win, core = loadCore(win)) {
    const audio = core.createAudio(win);
    let sdk = null;
    // CrazyGames asks not to report gameplay around tab switches.
    const session = core.createSession({
      loaded: () => sdk?.game?.loadingStop?.(),
      gameplay: active => {
        if (!sdk) return;
        if (active) sdk.game?.gameplayStart?.();
        else sdk.game?.gameplayStop?.();
      },
    }, { visibility: false });
    // The player's CrazyGames "mute audio" setting silences the game until cleared.
    // No ads are shown yet; adStarted/adFinished will pause through audio.pause().
    const applySettings = settings => audio.set('muted', Boolean(settings?.muteAudio));

    function connect() {
      const candidate = win.CrazyGames?.SDK;
      if (!candidate?.init) return Promise.resolve(null);
      return Promise.resolve(candidate.init()).then(() => {
        sdk = candidate;
        sdk.game?.loadingStart?.();
        if (session.ready) sdk.game?.loadingStop?.();
        if (session.active) sdk.game?.gameplayStart?.();
        applySettings(sdk.game?.settings);
        sdk.game?.addSettingsChangeListener?.(applySettings);
        return sdk;
      }, error => {
        // Ad blockers and unsupported hosts reject init; the game still runs.
        win.console?.warn?.('CrazyGames SDK unavailable', error);
        return null;
      });
    }

    return { audio, session, connect, get sdk() { return sdk; } };
  }

  if (typeof module === 'object' && module.exports) module.exports = { create };
  else if (typeof window === 'object') {
    const adapter = create(window);
    window.glob2CrazyGames = adapter;
    adapter.connect();
    window.glob2PortalCore.watchShell(window, adapter.session);
  }
})();
