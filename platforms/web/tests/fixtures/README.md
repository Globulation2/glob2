# Browser fixtures

## Replay import

`cross-replay.replay` is an import/playback fixture recorded at file format 213
from the repository's unchanged `content/games/cross-replay.game.gz` (seed 42), for 1,500
ticks. The replay reader's current minimum accepted format is 213
(`FILE_FORMAT_VERSION_RESOURCE_CLAIMS`).

Re-record when the accepted replay floor changes, using an isolated profile:

```sh
GLOB2_USER_DATA_DIR="$PWD/artifacts/browser-fixture-profile" \
GLOB2_TEST_SEED=42 \
GLOB2_REPLAY_PATH="$PWD/platforms/web/tests/fixtures/cross-replay.replay" \
SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy \
./build/darwin/client/release/src/glob2 game repeat content/games/cross-replay.game.gz --ticks 1500 --runs 1
```

Use the corresponding native build path on other platforms.

## Studio native/browser checksums

`studio-scenario.json` and `generator-studio-scenario.json` name the inputs of the
two Studio determinism scenarios and nothing else: no trace digests, engine
versions or sim keys. `studio.spec.js` and `generator-studio.spec.js` run each
scenario in both browser runtimes and in the native client of the same checkout
(`tests/native-reference.js`), and require every byte of the browser checksum
trace to equal the native one. A simulation change therefore moves both sides
together and needs no fixture refresh.

- `studio-scenario.json`: the AI Studio starter controller against an opponent
  on a map, by seed and tick count.
- `generator-studio-scenario.json`: the single-module Swamp package, its settings,
  the AI that plays every colony and the tick count. The spec traces the generated
  game and the same game reloaded from its initial save, and checks the preview
  report's package hash against `map inspect-package` and its sim version against
  `info sim-version`. The frozen preview world has no native CLI counterpart, so
  the serial and threaded runtimes must freeze the same world; the native trace of
  the generated game covers generation itself.

The native client is `build/linux/client/release/src/glob2`, which the browser CI
jobs unpack from the `web-native` artifact; set `GLOB2_NATIVE_BINARY` to use
another build. It runs from the repository root with a throwaway profile, and its
outputs land in `artifacts/native-reference/`. Locally, build the native client
and the browser runtimes from the same revision, then run
`npx playwright test studio.spec.js generator-studio.spec.js` from `platforms/web/`.

## Presentation surface ownership

`presentation-surface-probe.c` is a standalone Emscripten lifecycle fixture.
It draws a software DOM canvas, replaces it with WebGL2, checks a red GPU pixel,
and switches back to DOM software without replacing the Wasm instance. Run it
in both serial and pthread modes when changing the surface bridge or SDK.

Compile with the pinned SDK's `emcc`, `--js-library platforms/web/canvas-size.js`,
`-sMIN_WEBGL_VERSION=2 -sMAX_WEBGL_VERSION=2 -sEXIT_RUNTIME=0`, and an output
under `artifacts/`. Add `-pthread -sPTHREAD_POOL_SIZE=1
-sOFFSCREENCANVAS_SUPPORT=1` for the threaded fixture. A host page must load
`platforms/web/presentation-surface.js`, provide `Module.canvas` and `executionMode`,
create `Module.glob2PresentationSurface`, and attach each pthread Worker through
its `attachWorker()` method before loading the generated runtime. Serve with
cross-origin isolation headers using `platforms/web/serve.py`. Completion reports
`Module.probe`; verify `ok`, token `0x13579`, two committed surface generations,
and a green software pixel. The fixture proves SDK surface ownership, not full
game rendering or SDL lifecycle integration.

To check fullscreen preservation, export the `callMain` runtime method, set
`noInitialRun:true`, and place the canvas under `presentation-host`. In a trusted
button handler invoke `Module._probe_fullscreen()`, wait for `fullscreenchange`,
then call `Module.callMain([])`. The fixture exercises actual SDK fullscreen
strategy/resize handling and simulated SDL teardown exits. After both switches,
assert `Module.probe.fullscreen`, status id `canvas`, and DOM fullscreen element
`presentation-host`. Exiting fullscreen must restore the current canvas without
accessing the retired surface or its shared metadata.
