# Distributed tournament validation

`validation-20260914.tar.gz` contains retained game/save/checksum evidence,
coordinator records, all five SSH hosts plus localhost pilot events, immutable
bundle manifests, worker packages, test logs and offline reports. Identical files
use tar hard links to keep the evidence compact. Every link stays within the archive.

`validation-20260914.index.json` lists each logical file's length and SHA-256, plus
the archive's SHA-256. The archive was reopened and every member was verified
against this index after creation.

Extract into an empty directory:

```sh
mkdir /tmp/glob2-validation
tar -xzf test/fixtures/tournaments/validation-20260914.tar.gz -C /tmp/glob2-validation
python3 -m tools.tournaments.ai_comparison reanalyze /tmp/glob2-validation/final-smoke-ai_comparison
```

Reanalysis needs no engine binary or SQLite. The archive includes build manifests;
the supplied executable/data bundles remain in the workspace's
`artifacts/tournaments-validation/bundles/`. Simulation can also be checked using
the retained initial saves with a locally built compatible production client and
`test/tournament_cli_integration.py --initial FILE --output DIR --ticks 2048`.

See [the validation record](../../../docs/tournaments-validation.md) for results,
commands, measured platform coverage, limitations and the initial failures that
led to the fixes. [The framework guide](../../../docs/tournaments.md) describes
the operational and data contracts.


`telemetry-merge-20260914.tar.gz` and its index contain the subsequent mainline
integration evidence: native/structured report equality, cross-platform traces,
93 generator telemetry cases per platform, and 42 localhost/five-host map jobs.
Extract it into a separate empty directory. Reanalyze a collected host experiment:

```sh
python3 -m tools.tournaments.generator_stress reanalyze /tmp/telemetry-merge/distributed-linux/devlaptop.local --seed 19 --draws 100
```

Every archive member was checked against its indexed length and SHA-256. Full binary
bundles remain in ignored `artifacts/tournaments-telemetry-merge/bundles/`; immutable
manifests are included in the archive.

`game-telemetry-20260914.tar.gz` and its index retain the gameplay/AI/performance
integration evidence: 17 localhost tournament jobs, all eight AIs, typed log
roundtrips, old-save checks and per-tick master comparisons. Extract into an empty
directory and run the normal offline reanalysis against `distributed/localhost`.
See [validation details](../../../docs/tournament-telemetry-validation.md).
