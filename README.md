# Maxima farming review evidence

Production source: Globulation2/glob2 commit `fb8af8a854ae12d7b616185331df01b754858b7b`. This branch contains review evidence only; it is not intended to merge into master.

## Reproduction

Build the source commit with `scons -j4 release=1 server=0`. Run from the repository root, substituting the native binary path:

```sh
BINARY --run-game --map-file EVIDENCE/arena.map --game-seed 53101 --ticks 8192 --player maxima --player nicowar --output-dir OUTPUT --telemetry checksums --replay true
```

Repeat with rice.map. The both-maxima case uses rice.map with both players set to maxima. For old-save, decompress `test/maxima/fixtures/save-continuation/checkpoint-30000-v115.game.gz` from the source repository, use `--load-game CHECKPOINT --ticks 30512`, and omit map/seed/player arguments. Each output directory must be fresh.

`platforms.json` contains full checksum-sidecar SHA-256 digests from macOS/ARM64 and both Linux/x86-64 hosts; all corresponding digests match. `*-ticks.tsv.gz` preserves per-tick detailed team/entity state hashes from macOS, generated with `test/compare_save_continuation.py:records`. `traces.json` records counts and complete-sidecar hashes. Source inputs are retained here; the older save fixture is already versioned in the source repository.

`continuation.json` and `old-save-continuation.json` record comparisons against uninterrupted execution. Current saves were taken at tick 4096; the older save was re-saved at 30256. Detailed team/entity records match after reload (aggregate checksums include header state). Regression and replay boundary test logs are included. Windows was not exercised.

## Experimental selection

`tournament-report.md`, `tournament-statistics.json` and `tournament-protocol.json` retain the six-policy paired follow-up. The selected policy is layout_mature4. The experiment comprised 2304 game outputs over 192 maps, with 1152 new games and 1152 accepted matched controls. The selected prototype source is preserved in selected-prototype.cpp.gz. Production generalized it to all Maxima teams; the two Maxima-vs-Nicowar validation cases matched the prototype's complete per-tick sidecars.

The experiment showed increased harvest alongside higher starvation; late-game evidence remains inconclusive. This is not a blanket safety or performance claim. Raw full tournament logs, replays, saves and immutable bundles remain in the author's retained workspace; they are not all uploaded in this compact review package. The included reproducible inputs and checksum traces cover the production compatibility checks.

## Post-review fixes

See [current-head review validation](review/README.md) for the later wood-reserve fixes and their new regression/cross-platform evidence. Earlier results above describe the original production-policy commit.
