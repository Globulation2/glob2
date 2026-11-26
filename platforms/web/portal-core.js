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
  // whenever the game is playable (loaded, visible and not held by the portal).
  // Portals that ask for gameplay events only around real pauses (CrazyGames)
  // pass {visibility: false} so tab switches change nothing.
  function createSession(handlers = {}, options = {}) {
    let reported = -1;
    let ready = false;
    let hidden = false;
    let held = false;
    let active = false;
    const sync = () => {
      const playable = ready && !hidden && !held;
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
      hold(isHeld) { held = Boolean(isHeld); sync(); },
      get ready() { return ready; },
      get active() { return active; },
    };
    return session;
  }

  // The shell's progress bar is the download state; body.ready marks the first
  // interactive frame (onApplicationStarted in shell.html).
  function watchShell(win, session) {
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
    doc.addEventListener?.('visibilitychange', () => session.visible?.(!doc.hidden));
    if (session.visible) session.visible(!doc.hidden);
    if (doc.body) observe();
    else doc.addEventListener('DOMContentLoaded', observe, { once: true });
  }

  const core = { createAudio, createSession, watchShell };
  if (typeof module === 'object' && module.exports) module.exports = core;
  else if (typeof window === 'object') {
    window.glob2PortalCore = core;
    createAudio(window);
  }
})();
