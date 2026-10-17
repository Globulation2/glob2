# Building-gradient team quotas: review evidence

Production branch: `codex/building-gradient-team-quota`, head `0f37e4aa9d86ba82b9872a12eaa4402dcff22c0e`, based on `486a2230a`.

`benchmarks/policies` contains the six-policy, 32-seed comparison on Holiday Island 2. `benchmarks/variety` contains 32 seeds each on SmallForTwo, Archipelago and Oazis, comparing global four against four per team with one and four compute participants. Each folder includes scripts, source snapshots, methods, aggregate/per-game results, plots and a compressed archive of all valid runs' commands, process timings, engine results and chronological tick samples. Extract `raw-runs.tar.gz` into its containing folder to rerun the analysis.

These performance measurements used an optimized private prototype on frozen engine revision `43b153739838207983c922ab4cbeed7773459de9`. They estimate the policy's effects and are not measurements of the final production PR binary. Different admission policies change how games develop; total CPU therefore includes different evolving worlds. Host-contention exclusions and their reasons are retained in the methods and the variety exclusion directory.

Production validation artifacts and tested commit identity are recorded separately under `validation`. Only macOS ARM64 was tested here. A separate CLI save/load continuation mismatch reproduces on the old global-four benchmark baseline; see the validation README for the retained failure evidence. Cross-platform checksum equivalence and actual weaker-device performance have not been established; maintainer playtesting remains outstanding.
