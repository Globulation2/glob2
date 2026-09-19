# Integration with the updated labour branch

The selected hospital policy (`29c9fdcc3`) and adopted surplus-worker towers
(`b806198f5`) were merged with upstream labour commit `8624e3b94` in `08b59bc63`.
Upstream adds reachable fruit, fitted force beliefs, army growth, gathered waves,
and its merge from master. The temporary diagnostic PR integration was not merged
into this production branch.

The runtime files merged without conflicts. Test conflicts were resolved by
retaining both sets of migration assertions, adding a combined version-109
hospital/fruit/force/wave migration check, and updating the reviewed literal
inventory and parameter count (664). A pre-existing structural assertion was
updated narrowly for upstream's wave/fruit policy reads, while continuing to
reject other strategy reads in those executors. No runtime behavior was changed
to accommodate that assertion.

The full Maxima runner passes all 19 suites: 17 native suites, the configuration
suite (14 tests), and the strategy-policy suite (12 tests). Broader Python
discovery passes 60 tests with two optional skips. Logs are retained here.
The configuration startup tests alone use `GLOB2_TEST_MAX_TICKS=512`.

Two engine runs exercise the merged code on Mac arm64 and Linux x86_64:

- The retained old version-109 initial save, 24,576 ticks. Hospital settings
  migrate to 0.6; newer fruit/force/wave policies retain their legacy settings.
- A fresh game on retained map `g15-s3001-nicowar/map-r0.map`, game seed 15001,
  players Maxima and Nicowar, 24,576 ticks. Hospital ratio is 0.6 and the new
  upstream policies are enabled.

**Both runs match across platforms at every tick**, including their detailed
entity records (see `comparison.json`).

Each run records every tick's aggregate checksum and a hash of its detailed
entity records. Commands, engine results and stream hashes are retained with the
records. The old input is `../compatibility/old-initial.game.gz`; the fresh map is
inside `../game-evidence.tar.xz`. Run commands from the corresponding built
checkout's root, replacing recorded machine-specific paths with local paths.

The 240-game ablation predates the upstream integration. These integration checks
do **not** re-estimate the combined branch's win rate. Windows remains untested,
and the separately documented late-game save/resume divergence is not resolved
by these cross-platform checks.
