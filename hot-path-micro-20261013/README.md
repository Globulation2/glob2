# Turret simulation-thread microprofiling

[Report](report.md) · [chart](hot-path.png) · [raw paired data](raw.csv). All native samples are on an active host; no quiet-host qualification is claimed. See report for scope, provenance and limits.

Reassemble `cat micro-evidence.tar.gz.part-* > micro-evidence.tar.gz`, verify archive SHA256 against manifest.json, then extract. Archive contains both frozen binaries, source patches/snapshots, scripts, full Callgrind thread dumps/cache simulation, raw native logs and host activity, build and correctness evidence.
