# Evidence: draw end screen (fix/draw-end-screen)

- `game-draw-*.png`: the end-of-game dialog opened by the production
  `GameGUI::checkWonConditions` with the local team tied (both `hasWon`) with a
  non-allied team, captured by `tools/mobile_gallery/capture.py --game-only`.
  `game-victory-*.png` are the existing victory captures from the same run.
- `before-hashes.json` / `after-hashes.json`: SHA-256 of the per-tick checksum
  sidecars, replays and timing-stripped `result.json` from `checksum-run.sh`
  (4-AI generated map, 3 compute threads, 6000 ticks; 3-AI A_big_pond, 1 thread,
  6000 ticks; legacy `-test-games-nox` 5000 ticks) and a `replay-run.sh`
  playback of the 4-AI replay. Before = origin/master 75ad507e7, after = the
  branch; macOS arm64 release builds. The two files are byte-identical.
- `hash_run.py` reduces a run directory to the hash file.
