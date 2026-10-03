# Browser platform development

Globulation 2 runs in a full-page browser client with campaigns, tutorials,
custom games, map editing, local saves and online play with desktop and mobile players.
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
python3 browser/serve.py 8765 --bind 127.0.0.1 --directory build/emscripten/client/release
```

Open http://127.0.0.1:8765. The game starts automatically and fills the page.
The pinned SDK is installed once per user and host/version; build outputs remain
checkout-local and ignored by Git. Shared source manifests in
`scons/sources.py` drive native and browser builds. Each target owns its
configuration, objects, compilation database, and signature database. Compatible
SDK library and port caches are shared across worktrees.
Native builds do not require Emscripten; browser builds do not probe system libraries.

`browser/toolchain.json` pins the SDK revision and version. `emsdk=/path/to/emsdk`
selects an already installed matching SDK. `GLOB2_DEV_MODE=isolated` restores
checkout-local SDK and library/port caches for independent verification.
See [development storage](../docs/development/reference.md#shared-development-storage)
for configuration and cleanup. Omit `release=1` for a debug build.
`python3 browser/build.py` remains a compatibility wrapper for the release build.
See [delivery contracts](../docs/browser/implementation.md) for output paths and
platform boundaries.

The default build packages two runtimes: the root `index.js`/`index.wasm` serial
fallback and `threaded/index.js`/`threaded/index.wasm`. Both load the same game
data packages from `assets/` (see below). Keep `index.html`, `loader.js`, both
runtime directories and `assets/` together when publishing. `python3 browser/package-static.py` produces
the versioned release package with verified gzip sidecars for both runtimes. `web-tests` additionally builds serial and
threaded `script-tests.js` harnesses.

The loader prefers real shared-memory threads when isolation and worker startup
checks succeed. Use `?threads=serial` to exercise the fallback. The loader
also observes actual pthread startup errors and falls back after a
two-minute startup timeout. It removes the startup watcher when the application
is ready; errors in an already running game do not restart it.
The threaded application owns simulation and worker pools on an application worker; the DOM
thread stays available for input, filesystem proxying and software presentation.
WebGL transfers its canvas to the application worker. Engine thread policies
remain shared with native builds.

Hosting must send `Cross-Origin-Opener-Policy: same-origin` and
`Cross-Origin-Embedder-Policy: require-corp`. The local server above and the
provided Caddy configuration set these headers. Hosting without isolation
selects the serial runtime automatically. Direct Google Cloud Storage release
URLs use this fallback. Set `GLOB2_BROWSER_PUBLIC_URL` to a Caddy or equivalent
HTTPS frontend for a threaded public release; the release workflow verifies its
isolation headers before advertising it. `glob2Diagnostics.snapshot()` reports
`executionMode`, `threadFallback`, `workerCount` (active engine background
threads, excluding the application worker), and worker-owned `renderContext`
metrics. Browser command-line hosts must await `Module.start(args)` for completion;
`Module.callMain()` alone does not wait for a threaded command to finish.
### Game data and loading

The build packs the files the game reads at run time into content-addressed
packages under `assets/` (`scons/web_assets.py`); build scripts, translation
tooling, documentation, icons and store screenshots stay out.
`python3 scons/web_assets.py --report` lists the size of each category and package.
`browser/asset-loader.js` downloads the `core` package while the WebAssembly
module streams in, and the game starts once both are ready. `core` holds what
the menus, the online hub and rooms need: the interface and menu sprites, the
font, English and every language's own name, maps, campaigns, scripts and every
simulation data file, so
the sim version and checksum traces are unchanged. Three of its files are smaller
browser copies checked in under `browser/assets/` (`browser/derive_assets.py`):
the font without its Chinese, Japanese and Korean outlines, the menu's still
backdrop as a JPEG and the wordmark without the area the menu never shows. The
build uses a copy only while it matches its source; regenerate them after
changing the font, those images or a language's own name.
The loading page shows megabytes, a percentage and an estimate of the time left.

The rest follows in the background once the main menu is up, most needed first:

- `game`, the in-game sprites. Until they arrive the menu shows the colony still
  instead of the live colony, and a match, the editor or a replay waits on its
  loading screen ("Loading game graphics"; the online match checklist says the
  same). The menu colony, the settings' building artwork and later matches pick
  them up when they arrive. A `?replay=` link loads them before the game starts.
- `menu-music`; the menu music starts when it arrives.
- `font-cjk`, the full font. The game reopens its fonts when it arrives, so
  Chinese, Japanese and Korean player names and chat get their glyphs (the core
  copy already has the characters of every language's own name). With a Chinese,
  Japanese or Korean interface it is a startup package instead; switching to one
  before it arrives shows missing glyphs until it does.
- `translations`, the other languages' full catalogs; English stands in until
  they arrive. An interface in another language loads them before the game starts.
- `music` and `hd`: the in-game music and the high-resolution artwork (WebGL2
  only, and only while that setting is on). The game reads them when a match or
  the editor starts, so on a first visit a match started before the artwork
  arrives uses the original artwork; the next match uses the high-resolution set.

Later packages download in parts of about 4 MB. Optional ones (music and
artwork) pause while a match is running and are skipped when the browser asks to
save data; `game`, `font-cjk` and `translations` are retried until they arrive. A package becomes
visible to the game only when complete. Native builds load everything at startup
as before (`ApplicationHost::assetPackageReady` is always true there).
`glob2Diagnostics.snapshot().assets` reports each package's state.

Package parts are kept in the browser's Cache Storage, so later visits read them
from the device. A new build changes only the names of the packages whose
content changed. `python3 browser/precompress.py` writes Brotli and gzip copies
of both runtimes' modules and scripts, the loader and the packages for servers that
serve precompressed files (`deploy/Caddyfile` does).

## Playing and saving

Use the game's Quit button to wait for final storage writes before closing.
If that write fails, the game offers Retry save or Quit without saving. Closing
or refreshing the browser tab directly cannot wait for asynchronous saves.
Campaign creation/editing also waits for durable storage before returning.


Use Tutorial, Campaign, Custom Game or Editor. Clicking the canvas focuses
keyboard input and enables music. Live resize updates the internal resolution
at frame boundaries in scheduled browser flows.
Add `?renderer=software` or `?renderer=webgl2` to the URL to force a renderer.
`?replay=<url>` downloads a replay while the game loads and opens it in the replay
viewer (the platform's "Watch in browser"; see
[match history and the web app](../docs/multiplayer/history-and-web.md#watch-in-browser)).
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
Online play goes through the online hub, as on desktop; matches run over the
platform's relay. LAN joining requires a certificate trusted by the browser;
browser hosting remains unavailable. See `docs/browser/gateway.md` for the transports. Refreshing or disconnecting during a match ends that player's participation. Voice chat is a no-op; music uses the
existing Vorbis mixer. Map fertility is staged privately before publication. Landscape previews run
on the shared native worker path in threaded builds and one candidate per UI
timer in the serial fallback; an individual fallback roll remains synchronous. WebGL2 reuses the existing GPU renderer through Emscripten compatibility glue;
there is no mobile UI adaptation.

Browser and desktop players in one match must run builds with the same sim
version; the platform and relay check it (see the
[turn protocol](../docs/multiplayer/turn-protocol.md)).

## Compatibility note

Map generation follows the current native `GenerationService`, including its
landscape picker and start-quality scoring. The browser services preview
candidates cooperatively in the serial fallback. Threaded browser builds use the
same preview workers as native builds. In the serial fallback an individual
roll can pause the UI; see [ADR 005](../docs/browser/adr-005-generation-randomness.md).

Saved-game compatibility remains durable. Replays must meet the current
`REPLAY_MINIMUM_VERSION_MINOR`; browser import tests use a separately recorded
fixture under `browser/tests/fixtures`, without replacing shared determinism
baselines. Online and LAN players fetch the room's map by its hash.

## Automated tests

Build the native client and `transport-test` target before running the full
suite. Editor generation tests read the native client's `--headless-catalog`
to find named landscapes in the picker; this prevents additions to the catalog
from silently changing which generator a test exercises. UI actions still use
real pointer and keyboard events.

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

Use `GLOB2_TEST_RENDERER=webgl2` or `software` for renderer-sensitive tests.
Dedicated renderer-contract tests select their own renderer explicitly.
CI selects software for the full Chromium behavior suite
and focused Firefox/WebKit startup, viewport, responsive-input, campaign-storage,
WSS/native cross-play, and per-tick simulation checks, then repeats the
renderer-sensitive startup, input, viewport, responsive-presentation and reload checks in Chromium WebGL2.
`rendering.spec.js` selects its own renderer and runs only in the full suite;
`build-artifact.spec.js` checks the shared WASM binary once, without repeating it
for each browser. Both renderers retain the separate visibility check. Otherwise
tests use automatic selection; some headless WebKit builds
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

### Performance investigation

Build with `scons target=web release=1 web_profile=1` to retain WebAssembly
function names in CPU profiles while keeping release optimization. Install the
browser test dependencies, serve the build, then run from the repository root:

```sh
GLOB2_PROFILE_FIXTURE=games/gd-bigarena-long.game.gz \
node browser/profile.js 'http://127.0.0.1:8765/?renderer=webgl2&threads=serial' \
  artifacts/browser-performance/serial
```

Omit `threads=serial` to measure the selected threaded runtime. The harness
imports the same save through the UI, warms up, and samples normal speed, 8×,
then pause in a fresh browser profile. It records the actual execution mode,
GPU/backend, fixture hash, tick throughput, browser-process CPU totals, and
CPU profiles for the page and every current worker. CPU percent is summed
across the launched browser's processes: 100% means one CPU core. Profile
durations are sampled elapsed time, including blocking, rather than CPU time.
Load the `.cpuprofile` files into Chromium developer tools to inspect stacks.

For the serial flat WebGL map, the harness records frame-boundary dimension
queries following draw calls as a draw-cadence proxy. These are CPU submission
timestamps, not physical display scanout. For local display-paced matches, the
diagnostics `frames` counter counts presentations, while `loop` counts
processed host turns, including paused turns that skip repainting. Software
and threaded runs have no WebGL draw-cadence record.
`GLOB2_PROFILE_MODES=normal,8x` selects a subset of the three modes in their
fixed order. `GLOB2_PROFILE_SECONDS` sets each sample's duration (default 20);
`GLOB2_PROFILE_WIDTH` and `GLOB2_PROFILE_HEIGHT` set the viewport (default
1200×900). `GLOB2_PROFILE_HEADED=1` opens a real window. On macOS the harness
defaults to Metal; `GLOB2_CHROMIUM_ANGLE` overrides it. Check the recorded GPU
before interpreting timings. Run without concurrent builds and repeat paired
measurements; profiling overhead and one fixture do not establish performance
on every browser or game stage.

Normal gameplay does not poll WebGL errors, because `getError()` can force GPU
synchronization. `?gl-errors=1` enables frame-boundary error collection for
renderer validation; `renderContext.error` is `null` when collection is
disabled and otherwise retains the most recent nonzero observed error. Use
this flag for error assertions, and omit it from performance measurements.

The browser steps simulation on the application host even in its threaded
runtime; background AI/gradient workers do not enable the native separate
simulation runner. Local match presentation uses animation-frame callbacks
independently of the 25 Hz simulation clock. Each callback consumes input once
and advances due ticks within a six-millisecond work budget, always allowing
one due tick to finish. Accelerated play batches ticks instead of scheduling a
nested timer per tick. An individual expensive tick can exceed the budget; it
is never interrupted. DOM viewport, visibility and presentation state is
sampled once per host turn. Relay matches retain their polling, catch-up and
draw cadence. Paused local maps repaint at most every 40 milliseconds unless
input or a resize requests an immediate redraw; input continues at display
rate. Menus and background polling retain their scheduled delays. The threaded
SDL canvas-resize callback also guards the pinned SDK's viewport query during
WebGL context loss. Resize requests remain pending until the host can restore
graphics and apply the new viewport.

### CI compiler caches

CI builds the browser in parallel jobs. `web-build` compiles the WebAssembly
client and `web-native` the native WSS server, router and transport fixtures; both hand
their outputs to the `web-test` matrix as artifacts, which runs the Chromium
suite in five shards (split by spec file) beside separate torus, rendering and
settings-storage jobs, two jobs each for Firefox and WebKit, six focused WebGL2
jobs, and the lifecycle suite. Each long WebGL2 reload case runs on its own
runner; new untagged reload cases run with the match reload group. `web-deploy` checks
self-hosting on its own runner. A spec that needs a native program must use one
packaged by `web-native`, or add it there.
The browser test matrix runs at most ten jobs at once so Linux test shards can
start promptly during a full workflow run.

`web-native` restores the main Ubuntu 24.04 native compiler cache read-only
for its router and transport fixtures. It also restores its own SCons objects,
keyed by the installed compiler binary, and recompiles `GlobalContainerArgs.cpp`
to refresh the build banner. Master and manual workflow dispatches save those
objects for later runs. Emscripten uses a separate bounded cache
through `EM_COMPILER_WRAPPER=ccache`, keyed by runner OS/architecture and the
pinned toolchain. Only master saves that compiler cache, after pruning unused
entries; the first run for a new toolchain starts cold. The WebAssembly build
also restores SCons object files from the most recent master build. It rebuilds
`GlobalContainerArgs.cpp` on every run so the compile-time banner stays current;
manual workflow dispatches can warm this object cache on their own branch.
Compiler contents and input files remain validated; no timestamp or time-macro
sloppiness is enabled. Cache statistics are printed for native and WASM builds.
The Emscripten ports/system-library directory and linked output are rebuilt on
fresh runners; compiler caching does not replace the browser or determinism tests.
The determinism test transfers its checksum file as Base64 to avoid serializing
millions of individual byte values through Playwright. The decoded bytes still
feed the same per-tick comparison against native platforms.
