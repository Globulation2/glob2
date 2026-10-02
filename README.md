# Evidence: simulation thread and Scene renderer

Review evidence for the threaded-renderer PR (Scene ports + simulation thread).
Not meant to be merged. Branch commits measured: `78f0978b1` (rebased on master
`7ca31c802`), macOS 26 / Apple M3 / Apple clang 21, release builds.

## parity/ — pixel parity against master

`run.sh` captures 30 software-rendered frames (`SoftwareRenderBenchmark -G`,
1280x800) from three late-game saves (30,000 and 15,000 ticks): map and GUI modes
at zoom 0.5/1/1.37, a selected building, flag and unit (selection panels and map
markers), and the inn placement tool. Master's benchmark was built from
`7ca31c802` with this branch's `test/SoftwareRenderBenchmark.cpp` overlaid (only
the `PROFILE_SELECT`/`PROFILE_TOOL` hooks differ). Master was captured twice to
measure run-to-run noise.

`compare.txt`: 29 frames byte-identical; 1 frame differs only inside a 16x13
region that also differs between the two master runs. `sha256.txt` lists all 90
captures. PNGs of five representative frames from each side are included.

## simthread/ — simulation equivalence

`check_sim_thread.py` runs five scenarios (two new 4-AI games, generated map,
v121 legacy save, save continuation) with master (`2d9da6be0` baseline binary) and
the branch, comparing per-tick checksum sidecars, replay bytes and final saves.

- `threaded-check.log`: branch with `GLOB2_SIM_THREAD=1` (simulation on its own
  thread) — PASS.
- `serial-check.log`: branch serial — PASS.
- `*-sha256.txt`: hashes of every compared output (baseline, baseline repeat,
  candidate).
- `resume-*.checksums.gz`, `legacy-v121-*.checksums.gz`: the per-tick sidecars of
  two scenarios, master vs threaded (identical).

## tsan/ — ThreadSanitizer

ThreadSanitizer builds (`CXXFLAGS="-g -fsanitize=thread"`).

- `windowed-before-fix.log`: windowed `-test-games` (4 AIs, 300 ticks) found the
  drawn map size written by the main thread while the simulation thread extracted
  it (fixed: it now goes through `ClientRequests`), plus a pre-existing
  `VoiceRecorder` flag race (fixed: atomics).
- `windowed-after-fix.log`: 400 ticks; only the `VoiceRecorder` report remained,
  before that fix was built.
- `headless-run-game.log`: `--run-game` with `GLOB2_SIM_THREAD=1`, 600 ticks — no
  reports.

## bench/ — frame rate and tick throughput

`run.sh SPEED serial|threaded normal|background SEED`: windowed `-test-games`
(balanced, Maxima/Cortex x2, 1024x768, software renderer) for 3000 or 12000 ticks
with `gameSpeed` preset 8 (3 ms ticks, render interval 5 in serial) or 10
(uncapped, render interval 16 in serial). `serial` sets `GLOB2_SIM_THREAD=0`.
`background` runs under `taskpolicy -c background` (efficiency cores, throttled
QoS) as a crude stand-in for a weak device. Frames were counted with a temporary,
uncommitted counter: `++benchFrames` at the top of `GameGUI::drawAll` and a
`BENCH frames=N wall_ms=T` line (wall time since `Engine::beginSession`) printed
with the end-of-game summary.

See `bench-matrix.txt`; summary in the PR description.
