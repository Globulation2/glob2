# Browser fixtures

## Replay import

`cross-replay.replay` is an import/playback fixture recorded at file format 134
from the repository's unchanged `games/cross-replay.game.gz` (seed 42), for 1,500
ticks. The replay reader's current minimum accepted format is 134.

Re-record when the accepted replay floor changes, using an isolated profile:

```sh
GLOB2_USER_DATA_DIR=/tmp/glob2-browser-fixture-profile \
GLOB2_TEST_SEED=42 \
GLOB2_REPLAY_PATH="$PWD/browser/tests/fixtures/cross-replay.replay" \
SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy \
./build/darwin/client/release/src/glob2 --nox games/cross-replay.game.gz 1500 1
```

Use the corresponding native build path on other platforms. This fixture is
separate from `test/baselines/`, whose files are shared with external determinism
tooling and must not be replaced just to keep browser import tests current.

## Studio native/browser checksums

`studio-trace.json` pins the map, source, seed, opponent and tick count for the
Studio determinism test. Its trace digest comes from native execution; both the
serial and threaded browser runtime must match every byte of that checksum trace.
It is a compatibility assertion, not a browser-generated snapshot.

For an intentional source, fixture or simulation change, build the native engine
and both browser runtimes from the same revision. From the repository root, use
an isolated output directory and profile:

```sh
GLOB2_USER_DATA_DIR="$PWD/artifacts/studio-reference/profile" \
SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy GLOB2_CHECKSUM_SIDECAR=1 \
./build/linux/client/release/src/glob2 --run-game \
  --map-file platform/apps/engine-agent/fixtures/ais/two.map.gz --game-seed 19 \
  --player javascript --ai-script 0:examples/javascript/studio-starter.js \
  --player numbi --ticks 1024 --replay true --telemetry checksums \
  --save initial --save final --output-dir artifacts/studio-reference/result
sha256sum examples/javascript/studio-starter.js \
  platform/apps/engine-agent/fixtures/ais/two.map.gz \
  artifacts/studio-reference/result/game.replay.checksums
wc -c artifacts/studio-reference/result/game.replay.checksums
```

Use the corresponding native build path on other platforms. Update the manifest
only after reviewing why the native trace changed; apply the repository's
simulation revision and golden-match requirements when simulation behavior
changes. Then run `npx playwright test studio.spec.js` from `browser/` against the
rebuilt engine. All six browser/runtime combinations must match the native digest.
Keep the native trace and browser traces as review evidence.
