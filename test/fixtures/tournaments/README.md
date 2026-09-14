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
