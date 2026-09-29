# Default gradient scheduling reference traces

These fixtures pin the eight-tick periodic gradient publication schedule introduced
in save/replay version 120. They were refreshed for version 121 after AI controllers
received independent random streams. `test/check_telemetry_simulation.py` compares
the same legacy starting saves against these records on Linux, Windows and local builds.
The previous telemetry-only fixtures remain available in `../team-stats/`.

References were generated from the named legacy checkpoints with
`--gradient-workers 0 --telemetry checksums`, and independently matched byte for
byte using `--gradient-workers 1`. Tick limits are encoded in the filenames except
`v108-reload-256`, which loads the retained v108 checkpoint at tick 1024 and runs
to 1280. Both executions use the default eight-tick delay. Compression uses gzip
with an mtime of zero. These are regression oracles for the new schedule, not
claims of compatibility with the previous immediate publication schedule.

The two game traces were refreshed against master `5252e1b09` after saved per-AI
random streams changed their trajectories in #398. The same legacy starting saves,
eight-tick delay and tick limits were used. Worker counts 0 and 1 produced
byte-identical records for each game, as did the integrated Boost replacement.
The v108 checkpoint trace did not change.
