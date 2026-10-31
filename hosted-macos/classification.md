# Hosted macOS failure classification

PR head `9b2162f3`, hosted merge `2eac6329`, master parent `e67cba82`.
[Hosted job](https://github.com/Globulation2/glob2/actions/runs/41244011872/job/124664682983).

Compilation succeeded. The job reports 1674 engine cases passed, 12 failed and 228 skipped; 1169 unit cases and all 18 CLI continuation checks passed.

The 12 failures are in baseline components unchanged by the recording PR:

- SystemLanguage (1): the test expects the legacy Brazilian Portuguese code `br` for `hi_IN,pt_BR,en_US`, but the baseline Hindi translation added by #1305 makes Hindi the correct available first preference.
- JavaScriptCompatibility and TeamStatsSave (2): assertions still expect format/replay floor 186 and protocol 72; baseline #1308 bumped these to 187 and 73.
- BuildingStock (3), IntrusivePopulationSave (2), DelayedGradientPublication (1), EntityRandomLifecycle (1), UnitCustomization (1): old-format synthetic adapters retain version numbers such as 150/159/164/166/167/174 while writing the current MapHeader. Baseline #1308 removed the current terrain-experiments list, so historical readers encounter missing/misaligned fields. Logs explicitly report missing `Game.MapHeader.experiments.count` or failed historical load assertions.
- RuntimeResources golden (1): baseline #1308 changed MapHeader version/checksum fields while the seeded resource trace still expects the prior checksum (3456291229 versus 3464679837 at tick zero). Resource definition hashes match.

`git diff --exit-code e67cba82e7 9b2162f3 -- <files in classification.json>` is empty for the affected gates, loaders, tests and resource golden. No recording test failure appears in the job summary. The subsequent native-resource evidence command fails on the same golden case, and missing JavaScript corpus upload is downstream of that failed evidence step.

No source edits or new native test executions were performed for this classification. Full sanitized job log is retained compressed; compact failure excerpts and job metadata are beside this report.
