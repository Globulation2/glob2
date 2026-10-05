# Scoped gradient scheduling reference traces

These fixtures pin the version-123 scoped invalidation and bounded escape refresh
policy combined with the eight-tick periodic gradient pipeline. The superseded
version-120 traces were removed with the test-suite cleanup and remain in git history
(commit `508942f08`, PR #390).

`test/check_telemetry_simulation.py` checks the `*.terrain.checksums.gz` traces
across CI platforms. These are the current terrain-property simulation baselines;
the version-123 traces remain unchanged as historical evidence. Canonical terrain
IDs participate in map checksums and property-based ecology can change subsequent
simulation behavior. The checker retains all original legacy save inputs and
compares complete sidecars, including the aggregate checksum and every team/entity
record, for fresh loads and the v108 checkpoint continuation.

Regenerate only the terrain traces with
`python3 test/check_telemetry_simulation.py PATH/TO/glob2 --update-fixtures`, then
verify without that flag using both the default and `--parallel-ai` modes.
`--output artifacts/NAME` retains commands, logs, traces and a hash manifest.

Generate from the legacy saves named by that script with `--run-game --load-game`
and `--telemetry checksums`, using the stop ticks encoded in the filenames.
For `v108-reload-256`, load the retained v108 checkpoint at tick 1024 and stop at
1280. Serial (`--gradient-workers 0`) and single-worker (`--gradient-workers 1`)
outputs were independently compared byte for byte. Both use the default
eight-tick publication delay. Compress sidecars with gzip mtime zero.

Current references intentionally change with the simulation policy; they do not
assert trajectory compatibility with version 120. The separate save-continuation
test checks newly saved states against uninterrupted execution.
