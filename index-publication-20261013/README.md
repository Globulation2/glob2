# Worker index and publication optimization

[Report](report.md) · [owner CPU scopes](owner-scopes.png) · [engine effects](engine-delta.png) · [raw samples](raw.csv). Read the individual controls, host overlap and rejected-intermediate evidence; this is not live qualification.

Concatenate `index-publication-evidence.tar.gz.part-*`, verify against manifest.json, extract. Frozen binaries are deduplicated in binary-store/; restore their relative paths using index-publication/binary-manifest.json. All intermediate/final runs, source patches, controlled-relink recipes, thread profiles, correctness/traces/saves/continuations and host observations are preserved.
