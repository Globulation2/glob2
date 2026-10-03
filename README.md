# Telemetry PRs #360 / #343 evidence

Base: master 1d6b8ca09 for checksums, golden and suites. #360: 675665f78. #343: 962f11859.
CPU benchmarks ran on the same PR code over base 4da094543.
Linux x86_64, GCC 15.2, `scons release=1 server=0`, SDL3 3.4.16. The machine was shared and under load (load average about 28), so CPU timings are noisy.

checksums/: `scripts/checksum_ab.py BASE PR OUT --ticks 15000` plays 6 maps x 2 seeds with all eight AIs.
Each scenario runs master and the PR with `--telemetry checksums` and with `--telemetry checksums --telemetry team-timeline`, and stores the SHA-256 of each per-tick sidecar.
`identical` means all four are byte-identical. The *-save-continuation files load benchmarks/oazis-11-teams-tick15000.game.gz (master, seed 31, 11 teams) and play to tick 18000.

benchmarks/:
- `scripts/bench.py`: ABBA pairs, single-threaded, engine-measured run CPU after a 256-tick warmup, 1536 ticks from the save above.
- `scripts/scope_bench.py`: per-call mean of the `telemetry.capture` (TeamStats::step) and `simulation.tick` scopes.

golden/: the FourSquares1 record and trace regenerated on master with `--update-fixtures`. The #343 trace is byte-identical; its record differs only in the sim-version digits and CRC.
