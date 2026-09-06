# Team-statistics save compatibility

`version88.game` is a 32×32 saved game with one worker, nine end-game samples,
and a checkpoint between live-statistics refreshes. It was generated with the
unmodified version-88 writer at upstream commit `845cef53`; its version was
not patched into a newer save. It contains no tournament or Maxima state.

- Size: 47809 bytes
- SHA-256: `2ecb40b5a16e1e56c0d7b2e499969964d5d3c1e4574fcdd10960d1e53a8ff316`

`version88.expected.txt` records the original loader's live-statistics outputs
before and after 64 statistics steps. `version84.expected.txt` records the same
trace for the repository's `games/gd-small-2ai.game`. The test runner compares
both traces exactly with the new loader, including the legacy snapshot timing.

## Run

```sh
scons -j8 release=1 server=0 team-stats-save-test
python3 test/run-team-stats-save-tests.py build/src/TeamStatsSaveHarness
```

The default run generates and loads new-format games at every one of the 32
sampling positions, wraps the 128-entry history ring, and checks two repeated
reloads against uninterrupted statistics updates on every following tick. It
also tests the new fields with text streams, rejects invalid indices and short
binary fields, and loads both older fixtures. Each process uses a disposable
profile and writable directory; preferences are checked for unchanged bytes
and modification time even when `rememberUnit=1` was present before the test.
Every Game used in load tests has a live headless GameGUI owner.

## Reproduce the original failure or regenerate the legacy fixture

Use a separate worktree so the working fix stays available. Run these commands
from the PR checkout:

```sh
git worktree add ../glob2-stats-baseline 845cef53
git show HEAD:test/TeamStatsSaveHarness.cpp > ../glob2-stats-baseline/test/TeamStatsSaveHarness.cpp
git show HEAD:test/run-team-stats-save-tests.py > ../glob2-stats-baseline/test/run-team-stats-save-tests.py
git show HEAD:src/SConscript > ../glob2-stats-baseline/src/SConscript
cd ../glob2-stats-baseline
scons -j8 release=1 server=0 team-stats-save-test
python3 test/run-team-stats-save-tests.py build/src/TeamStatsSaveHarness
```

The original serializer fails the first restored population/health comparison.
The same generated-game test passes on the fixed checkout. To generate an
older-format fixture with that baseline executable:

```sh
python3 test/run-team-stats-save-tests.py build/src/TeamStatsSaveHarness \
  --write-fixture /absolute/path/version88.game
```

The generated fixture has the same test scenario; its header seed comes from
the engine's default initialization, so its bytes need not match the bundled
fixture. `--legacy /absolute/path/version88.game` prints the compatibility trace.

## Save-format scope

The standalone upstream change increments format 88 to 89. Readers of older
saves follow the same legacy path; new saves restore all live history entries,
smoothing entries and both sampling indices without advancing the statistics.
Older clients cannot read the new format. The network message layout is unchanged.

This fixes the engine statistics discontinuity identified at Numbi's inn
creation decision. It does not claim to repair every AI's save state or establish
full-game replay/save determinism by itself. The tournament-specific 20,000-tick
check additionally depends on separate persistence repairs outside this PR.
