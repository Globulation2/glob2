// SPDX-License-Identifier: GPL-3.0-or-later
// Yandex Games portal adapter, injected only by platforms/web/package-portal.py
// after the Yandex Games SDK (/sdk.js, served by Yandex) and portal-core.js and
// before every game script. Yandex requires the interface language to follow
// environment.i18n.lang, LoadingAPI.ready() once the game is playable, and
// GameplayAPI.start/stop around gameplay; game_api_pause/resume silence the game.
(() => {
  'use strict';

  function loadCore(win) {
    return typeof module === 'object' && module.exports ? require('./portal-core') : win.glob2PortalCore;
  }

  // The shell picks the browser locale with Glob2I18n (i18n.js) and passes it to
  // the game as SDL_PREFERRED_LOCALES when saves are restored, unless the player
  // saved a language in the game. Setting the Yandex language there makes it the
  // startup locale the same way a cookie or browser language would.
  function applyLanguage(win, lang) {
    const i18n = win.Glob2I18n;
    if (!i18n || typeof lang !== 'string' || !lang) return Promise.resolve(null);
    const code = i18n.resolveLocale([lang]);
    return Promise.resolve(i18n.initialize?.()).then(() => {
      if (!code || code === i18n.getLocale()) return code;
      return Promise.resolve(i18n.setLocale(code)).then(() => code);
    }).catch(error => {
      win.console?.warn?.('Could not apply the Yandex Games language', error);
      return null;
    });
  }

  function create(win, core = loadCore(win)) {
    const audio = core.createAudio(win);
    let sdk = null;
    const session = core.createSession({
      loaded: () => sdk?.features?.LoadingAPI?.ready?.(),
      gameplay: active => {
        const api = sdk?.features?.GameplayAPI;
        if (active) api?.start?.();
        else api?.stop?.();
      },
    });

    function connect() {
      if (!win.YaGames?.init) return Promise.resolve(null);
      return Promise.resolve(win.YaGames.init()).then(ysdk => {
        sdk = ysdk;
        ysdk.on?.('game_api_pause', () => { audio.pause(); session.hold(true); });
        ysdk.on?.('game_api_resume', () => { audio.resume(); session.hold(false); });
        const language = applyLanguage(win, ysdk.environment?.i18n?.lang);
        if (session.ready) ysdk.features?.LoadingAPI?.ready?.();
        if (session.active) ysdk.features?.GameplayAPI?.start?.();
        return language.then(() => ysdk);
      }, error => {
        win.console?.warn?.('Yandex Games SDK unavailable', error);
        return null;
      });
    }

    return { audio, session, connect, get sdk() { return sdk; } };
  }

  if (typeof module === 'object' && module.exports) module.exports = { create, applyLanguage };
  else if (typeof window === 'object') {
    const adapter = create(window);
    window.glob2Yandex = adapter;
    adapter.connect();
    window.glob2PortalCore.watchShell(window, adapter.session);
  }
})();
