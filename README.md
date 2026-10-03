# Evidence for Globulation2/glob2#229 (unit sprite sheets)

- `warm.txt`: `sheetbench` timing `Sprite::load("data/gfx/unit")` six times from
  the source tree (per-frame PNG), a runtime export without sheets (per-frame WebP)
  and the runtime export with sheets. Linux, release build, warm page cache.
- `frame-dumps.sha256`: SHA-256 of every loaded frame's RGBA dump (both layers,
  all 1,792 frames) from the three loads; all three are identical.
- `game-unit-opens.txt`: unit-sprite files the game opened (`strace -e openat`)
  running `glob2 -test-games` from the sheet export. No per-frame unit file was opened.
- `test-game-software.png`, `test-game-gpu.png`: `glob2 -test-games` (`-G` and
  `-g`) under Xvfb, from the sheet export, after about 50 s.
- `unit.sheet`: the generated index.
