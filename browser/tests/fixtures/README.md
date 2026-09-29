# Browser replay import fixture

`cross-replay.replay` is an import/playback fixture recorded at file format 100
from the repository's unchanged `games/cross-replay.game` (seed 42), for 1,500
ticks. The replay reader's current minimum accepted format is 99.

Re-record when the accepted replay floor changes, using an isolated profile:

```sh
GLOB2_USER_DIR=/tmp/glob2-browser-fixture-profile \
GLOB2_REPLAY_PATH="$PWD/browser/tests/fixtures/cross-replay.replay" \
SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy \
./build/darwin/client/release/src/glob2 --nox games/cross-replay.game 1500 1
```

Use the corresponding native build path on other platforms. This fixture is
separate from `tests/baselines/`, whose files are shared with external determinism
tooling and must not be replaced just to keep browser import tests current.
