// SPDX-License-Identifier: GPL-3.0-or-later
// GamePix portal adapter, injected only by platforms/web/package-gamepix.py after
// the GamePix SDK and before every game script. It reads the shell's own loading
// state from the page instead of hooking the runtime, so the shared shell keeps no
// portal code. GamePix requires pause/resume handlers that mute audio; they must be
// idempotent, so paused state is a flag rather than a counter.
(() => {
  'use strict';

  function create(win) {
    const contexts = new Set();
    let paused = false;

    // Every Web Audio context the game makes (SDL sound effects and the music
    // backend) is created after this script runs, so a subclass sees them all.
    const Native = win.AudioContext || win.webkitAudioContext;
    if (Native) {
      class TrackedAudioContext extends Native {
        constructor(...args) {
          super(...args);
          contexts.add(this);
          // The game reactivates audio when the page becomes visible; an ad can
          // still be showing then, so keep it silent until GamePix resumes.
          this.addEventListener?.('statechange', () => {
            if (paused && this.state === 'running') this.suspend().catch(() => {});
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

    function pause() {
      paused = true;
      for (const context of contexts) if (context.state === 'running') context.suspend().catch(() => {});
    }
    function resume() {
      paused = false;
      // A hidden page stays suspended; the game's own visibility handler resumes it.
      if (win.document.hidden) return;
      for (const context of contexts) if (context.state === 'suspended') context.resume().catch(() => {});
    }

    let reported = -1;
    let started = false;
    function progress(percent) {
      const value = Math.max(0, Math.min(100, Math.floor(Number(percent) || 0)));
      if (started || value <= reported) return;
      reported = value;
      win.GamePix?.lifecycle?.loading?.(value);
    }
    function start() {
      if (started) return;
      progress(100);
      started = true;
      win.GamePix?.lifecycle?.loaded?.();
      win.GamePix?.lifecycle?.gameStart?.();
    }

    return { pause, resume, progress, start, get paused() { return paused; }, contexts };
  }

  // The shell's progress bar is the download state; body.ready marks the first
  // interactive frame (onApplicationStarted in shell.html).
  function attach(win, adapter) {
    const doc = win.document;
    win.GamePix?.events?.on?.('pause', adapter.pause);
    win.GamePix?.events?.on?.('resume', adapter.resume);
    const observe = () => {
      const bar = doc.getElementById('loading-bar');
      const read = () => {
        if (bar) adapter.progress(bar.getAttribute('aria-valuenow'));
        if (doc.body.classList.contains('ready')) adapter.start();
      };
      const observer = new win.MutationObserver(read);
      if (bar) observer.observe(bar, { attributes: true, attributeFilter: ['aria-valuenow'] });
      observer.observe(doc.body, { attributes: true, attributeFilter: ['class'] });
      read();
    };
    if (doc.body) observe();
    else doc.addEventListener('DOMContentLoaded', observe, { once: true });
  }

  if (typeof module === 'object' && module.exports) module.exports = { create, attach };
  else if (typeof window === 'object') {
    const adapter = create(window);
    window.glob2GamePix = adapter;
    attach(window, adapter);
  }
})();
