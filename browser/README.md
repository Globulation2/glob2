# Browser platform development

Globulation 2 runs in a full-page browser client with campaigns, tutorials,
custom games, map editing, local saves and YOG cross-play.
WebGL2 is the default where the browser accelerates it; otherwise the game uses
the software renderer. `?renderer=webgl2` or `?renderer=software` forces either
one. The pinned Emscripten 4.0.15 build
shares game logic and the GPU renderer with desktop. The browser host schedules frames and cooperative jobs without Asyncify. Build and verification instructions below cover the desktop browser target.
The browser ADRs under `docs/browser` describe the implementation boundaries and
platform boundaries.
Browser SDK calls for viewport metrics and text editing live in
`browser/ApplicationHost.cpp`; shared UI code uses the `ApplicationHost` and
`BrowserTextInput` interfaces, with native implementations in libgag.

The desktop browser uses the system mouse cursor, including when an older profile
had enabled the game cursor. Menus retain their colony background after leaving
a match; the custom-game lobby draws its own panel over that background. Menus
and gameplay share the native responsive presentation. Touch capability,
logical viewport size, safe areas and interface scale determine the layout;
device-pixel ratio only determines rendering resolution. Physical phone and tablet
review remains necessary before release.

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
platform boundaries.

## Playing and saving

Use the game's Quit button to wait for final storage writes before closing.
If that write fails, the game offers Retry save or Quit without saving. Closing
or refreshing the browser tab directly cannot wait for asynchronous saves.
Campaign creation/editing also waits for durable storage before returning.


Use Tutorial, Campaign, Custom Game or Editor. Clicking the canvas focuses
keyboard input and enables music. Live resize updates the internal resolution
at frame boundaries in scheduled browser flows.
Add `?renderer=software` or `?renderer=webgl2` to the URL to force a renderer.
With WebGL2, press G in a match for the torus overview. Both rendering paths
support the flat map camera's zoom and picking. Native HTML text fields handle
browser keyboard editing, selection, paste, composition and password masking;
visual-viewport changes occlude dialogs without changing the gameplay layout.
Interface settings offer Automatic, Compact and Spacious. Compact remains usable
with a mouse and keyboard; attaching a mouse does not replace touch-sized controls.
High-quality graphics (including clouds) default to off for new browser profiles.
You can enable them in Settings; existing saved preferences are preserved.

Manual game/editor saves wait for durable IndexedDB persistence and offer retry
and export on failure. Saves belong to this browser profile and origin
(including the port); clearing site data deletes them. Use the in-game import
and export controls for backups. See [storage](../docs/browser/storage.md) for
format validation, campaign backups and remaining legacy-writer limitations.
Settings also waits for durable preferences/keyboard storage and offers Retry or
Continue on failure; Continue does not confirm a saved copy.

## Scope

The browser client uses mouse and keyboard controls.
The YOG entry uses the WebSocket gateway; LAN remains unavailable in browsers.
The lobby uses YOG chat; the separate native IRC bridge is unavailable.
See `docs/browser/gateway.md` for routing. Refreshing or disconnecting during a match ends that player's participation. Voice chat is a no-op; music uses the
existing Vorbis mixer. Map fertility is staged privately before publication. Landscape previews run
one candidate per UI timer; an individual generator roll remains synchronous. WebGL2 reuses the existing GPU renderer through Emscripten compatibility glue;
there is no mobile UI adaptation.

Browser and desktop multiplayer clients and YOG must use the same protocol
(version 41). Update all components together. See the [admission contract](../docs/browser/protocol.md).
Guests, invitations and coordinated refresh/reconnect recovery remain unfinished.

## Compatibility note

Map generation follows the current native `GenerationService`, including its
landscape picker and start-quality scoring. The browser services preview
candidates on its UI thread instead of starting native worker threads. An
individual roll can pause the UI; see [ADR 005](../docs/browser/adr-005-generation-randomness.md).

Saved-game compatibility remains durable. Replays must meet the current
`REPLAY_MINIMUM_VERSION_MINOR`; browser import tests use a separately recorded
fixture under `browser/tests/fixtures`, without replacing shared determinism
baselines. YOG distributes the host-selected map bytes to every player.

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

On Linux CI or a container without a desktop session, Firefox needs Xvfb for
WebGL2 and an audio service for `AudioContext.resume()` to complete. After
installing the Playwright browser dependencies, use:

```sh
sudo apt-get install -y pulseaudio
pulseaudio --start --exit-idle-time=-1 --load='module-null-sink sink_name=glob2_ci'
GLOB2_FIREFOX_HEADED=1 xvfb-run -a npm test
```

The null sink processes audio silently. On a workstation with an existing sound
server, use that server instead. `GLOB2_FIREFOX_HEADED=1` affects Firefox only;
the tests still require actual WebGL2 and audio activation. It does not bypass
assertions or select the software game renderer. Other environments retain the
default headless browser configuration.

For the separate real-window visibility suite in a Linux container, set `CI=1`
and run `xvfb-run -a npx playwright test --config visibility.config.js`. Its local
test browser then uses `--no-sandbox` and SwiftShader; these settings affect only
the test process and do not establish hardware GPU performance.

Use `npm test -- --project=chromium` for a focused run. The package lock pins the
test runner and its browser revisions. Failures retain traces and screenshots
under `build/browser-test-results`. WebKit automation does not substitute for
release testing in actual Safari, nor Chromium for Edge.

Run the suite with `GLOB2_TEST_RENDERER=webgl2` or `software` to force a renderer
throughout. CI selects software explicitly for the full Chromium behavior suite
and focused Firefox/WebKit checks, then runs the focused Chromium WebGL2 suite
separately. Otherwise tests use automatic selection; some headless WebKit builds
conceal the GPU identity and can select emulated WebGL2. Linux WebKit WebGL2
high-density resizing has shown intermittent stalls and graphics-process exits;
that combination is not covered by the passing software checks. Use
`?renderer=software` if affected.
On macOS, `GLOB2_CHROMIUM_ANGLE=metal` runs Chromium checks on the actual
Metal GPU instead of its headless SwiftShader backend. Record which backend was
used when reporting graphics results; emulated-GPU timings are not desktop
performance measurements.
Dedicated renderer tests exercise resize and actual context loss/restoration.
New multiplayer features, including reconnect recovery, are outside this change.

Build outputs and the SDK are ignored local files. Serve the output directory;
opening the HTML as a `file:` URL is unsupported. The SDL audio backend still
uses deprecated ScriptProcessorNode.
