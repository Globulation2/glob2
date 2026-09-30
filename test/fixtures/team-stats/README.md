# Team-statistics compatibility fixtures

`version88.game.gz` was generated with the unmodified version-88 writer at
`845cef53`. It contains one worker, nine end-game samples, and a checkpoint
between live-statistics refreshes on a 32×32 map.

- Raw (uncompressed) size: 47,809 bytes
- Raw SHA-256: `2ecb40b5a16e1e56c0d7b2e499969964d5d3c1e4574fcdd10960d1e53a8ff316`
- Checked-in gzip (level 6) size: 2,004 bytes

The expected text files record the original loader's live-statistics outputs
before and after 64 statistics steps for this fixture and the repository's
version-84 `games/gd-small-2ai.game.gz`. The shared regression runner compares
stdout exactly against these traces; see the commands in `test/README.md`.

`TeamStatsSaveHarness PROFILE ROOT --write-fixture FILE` generates a fixture
with the linked engine's writer, as a raw file; gzip it (`gzip -kn6 FILE`) to
match the repository's checked-in format. The `TeamStatsSave` legacy cases
inflate each checked-in `.gz` fixture in memory (`glob2test::inflated()`), print
its compatibility trace and compare it with the golden text; see `test/README.md`.
Use a disposable profile whose name starts with `glob2-save-test-`.
