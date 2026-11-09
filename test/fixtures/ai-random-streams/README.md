# AI random-stream reference

`numbi-castor-v121.game.gz` is a version-121 initial save with Numbi and
Castor on symmetric-arena generator 15, map seed 42, game seed 19, width and
height 7, and two teams.

`test/check_telemetry_simulation.py` runs it for 2,048 ticks under the default
eight-tick gradient schedule on Linux, Windows and local builds, and
`tools/sim_fingerprint.py` (part `saves`) compares its per-tick checksums between
builds, covering Numbi's AI-owned random stream. No sidecar is committed.
