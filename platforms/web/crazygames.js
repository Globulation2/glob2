// SPDX-License-Identifier: GPL-3.0-or-later
// CrazyGames portal adapter (HTML5 SDK v3), injected only by
// platforms/web/package-portal.py after the SDK and portal-core.js and before every
// game script. The SDK must finish init() before any other call, so the loading
// and gameplay state the shell reached meanwhile is replayed once it resolves.
// Touch or mouse presentation follows the SDK's device type, as CrazyGames asks,
// instead of the shell's touch detection (maxTouchPoints, any-pointer).
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
    // CrazyGames: "rely on our system info", not touch detection, for mobile
    // controls (docs.crazygames.com/requirements/technical/). shell.html reads
    // glob2PresentationOverride in publishPresentationMetrics; an unknown type
    // leaves the shell's own detection.
    const applyDevice = type => {
      if (!['desktop', 'tablet', 'mobile'].includes(type)) return null;
      win.glob2PresentationOverride = {...win.glob2PresentationOverride, touch: type !== 'desktop'};
      if (typeof win.dispatchEvent === 'function' && typeof win.Event === 'function')
        win.dispatchEvent(new win.Event('glob2-presentation-override'));
      return win.glob2PresentationOverride.touch;
    };
    // The player's CrazyGames "mute audio" setting silences the game until cleared.
    const applySettings = settings => audio.set('muted', Boolean(settings?.muteAudio));

    // Midgame ads at the natural pauses the game reports (portal-core.js
    // createBreaks), never before the first match. The game is held from the
    // request, as CrazyGames asks, and muted only once adStarted fires. The SDK
    // enforces its cooldown (adCooldown) and answers adsDisabled in Basic Launch;
    // every adError just ends the break.
    const breaks = core.createBreaks(win, {
      session, audio, muteOnStart: true,
      show: ad => {
        if (!sdk?.ad?.requestAd) return null;
        return new Promise(resolve => sdk.ad.requestAd('midgame', {
          adStarted: () => ad.started(),
          adFinished: () => resolve(true),
          adError: error => resolve(error?.code || false),
        }));
      },
    });
    core.installHost(win, breaks);

    function connect() {
      const candidate = win.CrazyGames?.SDK;
      if (!candidate?.init) return Promise.resolve(null);
      return Promise.resolve(candidate.init()).then(() => {
        sdk = candidate;
        try { applyDevice(sdk.user?.systemInfo?.device?.type); }
        catch (error) { win.console?.warn?.('CrazyGames system info unavailable', error); }
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

    return { audio, session, breaks, connect, applyDevice, get sdk() { return sdk; } };
  }

  if (typeof module === 'object' && module.exports) module.exports = { create };
  else if (typeof window === 'object') {
    const adapter = create(window);
    window.glob2CrazyGames = adapter;
    adapter.connect();
    window.glob2PortalCore.watchShell(window, adapter.session);
  }
})();
