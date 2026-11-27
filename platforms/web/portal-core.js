// SPDX-License-Identifier: GPL-3.0-or-later
// Shared runtime for web-portal uploads, injected only by
// platforms/web/package-portal.py before every game script. It reads the shell's
// own loading state from the page instead of hooking the runtime, so the shared
// shell keeps no portal code. Each portal adapter (gamepix.js, crazygames.js, ...)
// turns these signals into its SDK's calls; Newgrounds and Game Jolt load this
// file alone.
(() => {
  'use strict';

  // Every Web Audio context the game makes (SDL sound effects and the music
  // backend) is created after this script runs, so a subclass sees them all.
  // Audio is silent while any reason holds it (a portal pause, a portal mute
  // setting); each reason is a flag, so repeated pauses and resumes are idempotent.
  function createAudio(win) {
    if (win.glob2PortalAudio) return win.glob2PortalAudio;
    const contexts = new Set();
    const reasons = new Set();
    const Native = win.AudioContext || win.webkitAudioContext;
    if (Native) {
      class TrackedAudioContext extends Native {
        constructor(...args) {
          super(...args);
          contexts.add(this);
          // The game reactivates audio when the page becomes visible; an ad can
          // still be showing then, so keep it silent until the portal resumes.
          this.addEventListener?.('statechange', () => {
            if (reasons.size && this.state === 'running') this.suspend().catch(() => {});
          });
        }
        close() {
          contexts.delete(this);
          return super.close();
        }
      }
      win.AudioContext = TrackedAudioContext;
      if (win.webkitAudioContext) win.webkitAudioContext = TrackedAudioContext;
    }

    function set(reason, silent) {
      if (silent) {
        reasons.add(reason);
        for (const context of contexts) if (context.state === 'running') context.suspend().catch(() => {});
        return;
      }
      reasons.delete(reason);
      // A hidden page stays suspended; the game's own visibility handler resumes it.
      if (reasons.size || win.document?.hidden) return;
      for (const context of contexts) if (context.state === 'suspended') context.resume().catch(() => {});
    }

    const audio = {
      set,
      pause: () => set('portal', true),
      resume: () => set('portal', false),
      get paused() { return reasons.has('portal'); },
      get silent() { return reasons.size > 0; },
      contexts,
    };
    win.glob2PortalAudio = audio;
    return audio;
  }

  // Loading and gameplay as portals want them: whole, increasing percentages,
  // one `loaded` when the main menu is interactive, then `gameplay(true/false)`
  // whenever the player is in a match that is playable (loaded, visible, not
  // paused or in the in-game menu, and not held by the portal or an ad). The game reports matches through
  // window.glob2PortalHost (createBreaks). Portals that ask for gameplay events
  // only around real pauses (CrazyGames) pass {visibility: false} so tab
  // switches change nothing.
  function createSession(handlers = {}, options = {}) {
    let reported = -1;
    let ready = false;
    let hidden = false;
    let playing = false;
    // Play stopped inside a match: the in-game menu or a pause ('pause'/'resume').
    let paused = false;
    let active = false;
    // Why the game is held: 'portal' (the SDK's own pause), 'ad' (a break we
    // requested). Each reason is a flag, so repeated holds are idempotent.
    const holds = new Set();
    const sync = () => {
      const playable = ready && playing && !paused && !hidden && !holds.size;
      if (playable === active) return;
      active = playable;
      handlers.gameplay?.(playable);
    };
    const session = {
      progress(percent) {
        const value = Math.max(0, Math.min(100, Math.floor(Number(percent) || 0)));
        if (ready || value <= reported) return;
        reported = value;
        handlers.progress?.(value);
      },
      start() {
        if (ready) return;
        session.progress(100);
        ready = true;
        handlers.loaded?.();
        sync();
      },
      visible(isVisible) {
        if (options.visibility === false) return;
        hidden = !isVisible;
        sync();
      },
      hold(isHeld, reason = 'portal') {
        if (isHeld) holds.add(reason);
        else holds.delete(reason);
        sync();
      },
      match(isPlaying) {
        playing = Boolean(isPlaying);
        // A match starts and ends in play; a pause belongs to one match.
        paused = false;
        sync();
      },
      pause(isPaused) { paused = playing && Boolean(isPaused); sync(); },
      get ready() { return ready; },
      get active() { return active; },
      get playing() { return playing; },
      get paused() { return paused; },
      get held() { return holds.size > 0; },
    };
    return session;
  }

  // Ad breaks at the natural pauses the game reports, for portal packages whose
  // policy carries ads (window.GLOB2_PORTAL.ads, package-portal.py). The game
  // calls window.glob2PortalHost.matchBreak(point) through ApplicationHost:
  //   'start'  before a match starts (after the player chose to play),
  //   'end'    the match is over and its results are showing,
  //   'leave'  the player left a finished match for the menu,
  //   'pause'  play stopped inside a match (the in-game menu or a pause),
  //   'resume' play continued.
  // points lists where a break may run an ad: 'start' and 'leave' by default,
  // the natural breaks between matches; Poki asks before every return to play
  // instead ('start' and 'resume'). 'end' and 'pause' never ask; a point not
  // listed only changes gameplay and never starts the minimum gap. The adapter's show(ad)
  // requests the SDK's interstitial and returns a promise that settles when the
  // break is over; it calls ad.started() when the ad really plays (some SDKs
  // want audio muted only then), reads ad.active to know whether the break still
  // waits for it, and returns null when it cannot ask. While a
  // break runs the session is held: gameplay is reported stopped and the game
  // reads glob2PortalHost.held to pause its simulation and input. Audio is
  // silenced from the request, or from ad.started() with {muteOnStart: true}.
  // firstStart allows a break before the first match since the page loaded
  // (GameDistribution's required pre-roll, Poki). minInterval (ms, 3 minutes by
  // default; 0 turns it off) is our own gap after every break, measured from
  // when it ended, whether or not an ad played, on top of the SDK's own
  // frequency rules: leaving a match and starting
  // the next is one break, never two back to back. A break that does
  // not start within startTimeout, or does not finish within maxDuration,
  // releases the game anyway, so a stalled SDK or an ad blocker never blocks it.
  function createBreaks(win, {session, audio, show, firstStart = false, muteOnStart = false, awaitInput = false,
                              points = ['start', 'leave'], minInterval = 180000, startTimeout = 10000, maxDuration = 180000, now = () => win.performance?.now?.() ?? Date.now()} = {}) {
    const enabled = win.GLOB2_PORTAL?.ads === true && typeof show === 'function';
    const asks = new Set(points.filter(point => point === 'start' || point === 'leave' || point === 'resume'));
    let starts = 0;
    let last = -Infinity;
    let current = null;
    let requests = 0;

    function release(token, requested = true) {
      if (current !== token) return;
      current = null;
      if (requested) last = now();
      win.clearTimeout?.(token.timer);
      audio.set('ad', false);
      session.hold(false, 'ad');
    }

    function request(point) {
      if (point === 'start' && starts++ === 0 && !firstStart) return false;
      if (!enabled || current || !asks.has(point)) return false;
      if (minInterval > 0 && now() - last < minInterval) return false;
      const token = {playing: false};
      const ad = {
        get active() { return current === token; },
        started() {
          if (current !== token || token.playing) return;
          token.playing = true;
          audio.set('ad', true);
          win.clearTimeout?.(token.timer);
          token.timer = win.setTimeout?.(() => release(token), maxDuration);
        },
      };
      token.ad = ad;
      // Held before show(): an SDK may pause the game synchronously.
      current = token;
      session.hold(true, 'ad');
      if (!muteOnStart) audio.set('ad', true);
      let pending;
      try { pending = show(ad, point); } catch (error) { pending = null; win.console?.warn?.('Ad break failed', error); }
      if (!pending) {
        release(token, false);
        return false;
      }
      requests += 1;
      if (!token.playing) token.timer = win.setTimeout?.(() => { if (!token.playing) release(token); }, startTimeout);
      Promise.resolve(pending).catch(error => win.console?.warn?.('Ad break failed', error))
        .then(() => release(token));
      return true;
    }

    // A match can start before the player has touched the page: a portal's
    // first visit opens the introduction lesson by itself. With awaitInput,
    // that start is reported, and its break asked for, only at the first key,
    // click or tap in the game's frame: Poki's Inspector flags a gameplayStart
    // without a player interaction, and GameDistribution's mobile ads must
    // start inside the player's tap. navigator.userActivation tells whether the
    // frame has had one; where a browser lacks it the start is reported at once.
    // CrazyGames leaves it off: its QA Tool times loading up to the first
    // gameplayStart, which must not include the player reading the briefing.
    // The break begins just after that input is released, so the press itself
    // still reaches the game (the click that closes the lesson briefing is not
    // swallowed by the hold) while the browser's transient activation, which
    // lasts seconds, still lets the ad play sound.
    const activation = awaitInput ? win.navigator?.userActivation : null;
    const inputs = ['pointerup', 'mouseup', 'touchend', 'keyup'];
    let waiting = false;
    function begin() {
      if (!waiting) return;
      waiting = false;
      request('start');
      session.match(true);
    }
    function playerArrived() {
      for (const name of inputs) win.removeEventListener?.(name, playerArrived, true);
      if (waiting) win.setTimeout(begin, 100);
    }
    function awaitPlayer() {
      if (waiting) return;
      waiting = true;
      for (const name of inputs) win.addEventListener?.(name, playerArrived, true);
    }

    const breaks = {
      // Returns true when an ad break holds the game; the game then waits for
      // held to clear before it starts the match.
      matchBreak(point) {
        if (point === 'start') {
          if (activation && !activation.hasBeenActive) {
            awaitPlayer();
            return false;
          }
          waiting = false;
          // Hold first, so gameplay is reported only once the break is over.
          const held = request('start');
          session.match(true);
          return held;
        }
        if (waiting && (point === 'end' || point === 'leave')) {
          // The match ended before the player touched it: nothing was reported.
          waiting = false;
          return false;
        }
        if (point === 'pause') {
          session.pause(true);
          return false;
        }
        if (point === 'resume') {
          // Only a return to play from a pause in a match; held before play
          // resumes, so gameplay starts only after the break.
          const held = session.paused ? request('resume') : false;
          session.pause(false);
          return held;
        }
        if (point === 'end' || point === 'leave') session.match(false);
        return point === 'leave' ? request('leave') : false;
      },
      // For SDKs that announce an ad's start and end as events (GameDistribution).
      started() { current?.ad.started(); },
      finished() { if (current) release(current); },
      get held() { return session.held; },
      get active() { return current !== null; },
      get requests() { return requests; },
      get enabled() { return enabled; },
    };
    return breaks;
  }

  // The object the game's ApplicationHost reaches (platforms/web/ApplicationHost.cpp).
  function installHost(win, breaks) {
    win.glob2PortalHost = {
      matchBreak: point => breaks.matchBreak(String(point)),
      get held() { return breaks.held; },
    };
    return win.glob2PortalHost;
  }

  // The shell's progress bar is the download state; body.ready marks the first
  // interactive frame (onApplicationStarted in shell.html). With {focus: true}
  // (Yandex Games, requirements 1.3 and 1.19.3) a window without focus counts as
  // a hidden page for gameplay, and audio is silenced until focus returns. The
  // simulation is not held for it: the shell pauses only a hidden page.
  function watchShell(win, session, {focus = false, audio = win.glob2PortalAudio} = {}) {
    const doc = win.document;
    const observe = () => {
      const bar = doc.getElementById('loading-bar');
      const read = () => {
        if (bar) session.progress(bar.getAttribute('aria-valuenow'));
        if (doc.body.classList.contains('ready')) session.start();
      };
      const observer = new win.MutationObserver(read);
      if (bar) observer.observe(bar, { attributes: true, attributeFilter: ['aria-valuenow'] });
      observer.observe(doc.body, { attributes: true, attributeFilter: ['class'] });
      read();
    };
    let focused = true;
    const update = () => {
      session.visible?.(!doc.hidden && focused);
      if (focus) audio?.set('blur', !focused);
    };
    doc.addEventListener?.('visibilitychange', update);
    if (focus) {
      focused = typeof doc.hasFocus === 'function' ? doc.hasFocus() : true;
      win.addEventListener?.('blur', () => { focused = false; update(); });
      win.addEventListener?.('focus', () => { focused = true; update(); });
    }
    update();
    if (doc.body) observe();
    else doc.addEventListener('DOMContentLoaded', observe, { once: true });
  }

  const core = { createAudio, createSession, createBreaks, installHost, watchShell };
  if (typeof module === 'object' && module.exports) module.exports = core;
  else if (typeof window === 'object') {
    window.glob2PortalCore = core;
    createAudio(window);
  }
})();
