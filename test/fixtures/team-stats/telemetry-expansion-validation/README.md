# Format-108 telemetry checkpoint

`checkpoint-1024-v108.game.gz` is a four-AI game (`games/gd-large-4ai.game`, game
seed 2) saved at tick 1,024 in save format 108. `test/check_telemetry_simulation.py`
loads it, simulates to tick 1,280 and compares each team/entity checksum record with
`test/fixtures/scoped-gradients/v108-reload-256.checksums.gz`. The uncompressed
SHA-256 is `a07502e108548d24009c7f058545b04f7cd65356b27f4f17920c835bf4c72f5d`.

The validation narrative, CPU benchmarks, logs and screenshots that accompanied the
telemetry expansion are in PR #310 (commit `e2def556c`), not in the tree.
