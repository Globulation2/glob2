# Orchard Commons review evidence

Download and extract [orchard-commons-evidence.tar.gz](orchard-commons-evidence.tar.gz). This branch holds bulk review evidence separately from implementation history.

Start with `country-update/README.md`. Final data are in `country-update/swales-study`, `country-update/retries`, `country-update/swales-gallery`, `country-update/final-play`, and `country-update/conversion`. The archive also retains earlier rotation results/logs, an earlier late-game save, reference-game results/logs, and the final real-engine conversion fixtures. Earlier rotation and reference final saves are omitted to limit download size; the final candidate and rotation 3 saves are included. No game binaries or personal profiles are included.

`manifest.json` lists every archived evidence file with size and SHA-256. Commands and parameter sets are retained with the runs; see the implementation branch's `docs/artifacts/orchard-commons/README.md` for the concise interpretation and reproducible entry points.

Archive SHA-256: `e937fe01517fece978616815d0782cab002e2d36d5bf46e4e3354d724ce3cfdd`

The manifest now includes the completed golden-check log; an earlier evidence commit hashed that log before it finished.
