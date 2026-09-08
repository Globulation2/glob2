# Single-player browser experiment

This target compiles Globulation 2 to WebAssembly with Emscripten 4.0.15.
It uses the SDL2 software renderer, Asyncify for browser event-loop yielding,
and IndexedDB for local saves. Multiplayer menu entries and voice recording
are disabled. Native builds keep their normal implementations.

## Build

From the repository root:

```sh
python3 browser/setup.py
scons target=web release=1 -j8
python3 -m http.server 8765 --bind 127.0.0.1 --directory build/emscripten/client/release
```

Open http://127.0.0.1:8765. The game starts automatically and fills the page.
The SDK and build output are ignored by Git. Shared source manifests in
`scons/sources.py` drive native and browser builds. Each target owns its
configuration, objects, compilation database, cache, and signature database.
Native builds do not require Emscripten; browser builds do not probe system libraries.

`browser/toolchain.json` pins the SDK revision and version. Boost headers come
from the SDK's checksum-verified Boost port. `emsdk=/path/to/emsdk` selects an
already installed matching SDK. Omit `release=1` for a debug build.
`python3 browser/build.py` remains a compatibility wrapper for the release build.
See [delivery contracts](../docs/browser/implementation.md) for output paths and
remaining release gates.

## Playing and saving

Use Tutorial, Campaign, or Custom Game. Clicking the canvas focuses keyboard
input and enables music. Save through the in-game menu; IndexedDB automatically
persists files when they close. Saves belong to this browser profile and
origin (including the port); clearing site data deletes them. Reload after
choosing Quit to restart. The game starts at the viewport resolution; resizing
the page scales the canvas proportionally and may add black bars.

## Scope

This is a desktop-browser experiment with mouse and keyboard controls.
The YOG entry uses the WebSocket gateway; LAN remains unavailable in browsers.
The lobby uses YOG chat; the separate native IRC bridge is unavailable.
See `docs/browser/gateway.md` for routing. Full matches and recovery remain experimental. Voice chat is a no-op; music uses the
existing Vorbis mixer. Map fertility calculation runs cooperatively on the
browser thread. There is no WebGL renderer rewrite or mobile UI adaptation.

`Module.glob2Tick`, `Module.glob2Screen`, and `Module.browserLog` expose
simulation and UI diagnostics for smoke checks without adding page controls.

## Verified experiment results

Tested in Chromium on 2026-09-07 (America/Toronto):

- Automatic startup into the native menu, with only a canvas on the page.
- Canvas and viewport both 1200x900; no HTML buttons or visible wrapper text.
- Custom match on A big pond with the default AI players: 24.93 simulation
  ticks/second over a 125-tick sample (normal target: 25 ticks/second).
- Keyboard pause stops simulation advancement.
- In-game save writes a 2,593,629-byte `.game` file. Automatic IndexedDB
  persistence restores identical SHA-256 bytes after page reload, and the
  saved match loads and resumes.
- Tutorial Campaign / Introduction and Basics launches; browser audio context
  runs after interaction. Audible output has not been independently checked.
- No uncaught JavaScript errors in the final custom-match/save/load smoke test.

## Automated tests

The maintained Playwright suite starts an isolated local HTTP server and uses
fresh browser profiles for every test. It covers page startup, a custom match,
pause over multiple observed engine frames, save persistence across reload,
loading and audio activation. Player actions use real mouse/keyboard input;
assertions read `glob2Diagnostics` without changing game state.

```sh
cd browser
npm ci --ignore-scripts
npx playwright install chromium firefox webkit
npm test
```

Use `npm test -- --project=chromium` for a focused run. The package lock pins the
test runner and its browser revisions. Failures retain traces and screenshots
under `build/browser-test-results`. These initial tests do not yet cover the
complete supported-release matrix. WebKit automation does not substitute for
release testing in actual Safari, nor Chromium for Edge.

The generated payload is about 26 MiB of assets, 12 MiB of WebAssembly, and
615 KiB of JavaScript, before HTTP compression. The build and SDK are local
outputs, not committed assets. Serve the output directory; opening the HTML
as a `file:` URL is unsupported.

This does not establish large-map/late-game performance, full campaign
completion, Safari/Firefox compatibility, native/browser replay determinism,
or a production-ready port. Resizing scales the initial game resolution.
The SDL audio backend emits a ScriptProcessorNode deprecation warning;
legacy diagnostics also write informational music messages to stderr.
