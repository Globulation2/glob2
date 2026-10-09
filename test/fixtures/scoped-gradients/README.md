# Scoped gradient scheduling reference traces

These fixtures pin the version-123 scoped invalidation and bounded escape refresh
policy combined with the eight-tick periodic gradient pipeline. The superseded
version-120 traces were removed with the test-suite cleanup and remain in git history
(commit `508942f08`, PR #390).

`test/check_telemetry_simulation.py` checks the `*.resources.checksums.gz`
traces across CI platforms. These are the format-152, simulation-revision-40
baselines. The version-123 and `*.terrain.checksums.gz` references remain unchanged
as historical evidence, including the simulation-revision-20 capability checksums.
All epochs use the same retained legacy save inputs.

The current epoch includes deterministic building area effects, 30-tick normal
simulation timing and persistent owner-specific random streams. Older saves
initialize missing random streams from their saved seed and identities. These
intentional simulation changes alter trajectories after loading the retained
legacy saves; the references describe their execution under the current engine.
Aggregate checksums also include serialized state and random-stream progress,
so historical hashes are not promises of cross-epoch simulation compatibility.

The checker compares complete sidecars, including the aggregate checksum and every
team/entity record, for fresh loads and the v108 checkpoint continuation. It does
not discard differing fields or weaken the per-tick comparison.

Regenerate only the resource traces with
`python3 test/check_telemetry_simulation.py PATH/TO/glob2 --update-fixtures`, then
verify without that flag using both the default and `--parallel-ai` modes.
`--output artifacts/NAME` retains commands, logs, traces and a hash manifest.

Generate from the legacy saves named by that script with `--run-game --load-game`
and `--telemetry checksums`, using the stop ticks encoded in the filenames.
For `v108-reload-256`, load the retained v108 checkpoint at tick 1024 and stop at
1280. Compare one and four compute workers (`--compute-threads 1` and
`--compute-threads 4`) byte for byte. Both use the default eight-tick
publication delay. Compress sidecars with gzip mtime zero.

Current references intentionally change with the simulation policy; they do not
assert trajectory compatibility with version 120. The separate save-continuation
test checks newly saved states against uninterrupted execution.
