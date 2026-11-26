// SPDX-License-Identifier: GPL-3.0-or-later
// Poki portal adapter, injected only by platforms/web/package-portal.py after the
// Poki SDK and portal-core.js and before every game script. PokiSDK.init() rejects
// under ad blockers; the game must still load, so every call waits on init and a
// rejection only leaves the SDK unused.
(() => {
  'use strict';

  function loadCore(win) {
    return typeof module === 'object' && module.exports ? require('./portal-core') : win.glob2PortalCore;
  }

  function create(win, core = loadCore(win)) {
    const audio = core.createAudio(win);
    let sdk = null;
    let finished = false;
    const finish = () => {
      if (!sdk || finished || !session.ready) return;
      finished = true;
      sdk.gameLoadingFinished?.();
    };
    const session = core.createSession({
      loaded: finish,
      gameplay: active => {
        if (!sdk) return;
        if (active) sdk.gameplayStart?.();
        else sdk.gameplayStop?.();
      },
    });

    function connect() {
      const candidate = win.PokiSDK;
      if (!candidate?.init) return Promise.resolve(null);
      return Promise.resolve(candidate.init()).then(() => {
        sdk = candidate;
        finish();
        if (session.active) sdk.gameplayStart?.();
        return sdk;
      }, error => {
        win.console?.warn?.('Poki SDK unavailable', error);
        return null;
      });
    }

    return { audio, session, connect, get sdk() { return sdk; } };
  }

  if (typeof module === 'object' && module.exports) module.exports = { create };
  else if (typeof window === 'object') {
    const adapter = create(window);
    window.glob2Poki = adapter;
    adapter.connect();
    window.glob2PortalCore.watchShell(window, adapter.session);
  }
})();
