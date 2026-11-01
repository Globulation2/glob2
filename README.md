# Maxima prestige draft PR evidence

Code commit: `857761d70d6c5783d6ddda3ab3d769756e1ca916`, integrated with master `8fd9edb0b413a9dc19618e75149458bf525506af`.

Adaptive prestige is enabled by default for normal prestige victory, with a1–4 preparation allowance. Final identity: SIM120, save193, network75. The user explicitly requested default activation.

## Current integration verification

[Verification and coverage](reports/pr-integration-verification.md) · [Source/binary hashes](reports/pr-source.json)

- 516 native cases and77 Python Maxima tests pass;310 Markdown files checked without errors.
- Development and optimized Linux builds pass; current save continuation and all128 per-tick world/replay/snapshot checksum records agree.
- Golden match verifies; protocol74 is explicitly unverifiable against75; old/current/future replay boundaries are covered.
- Other supported-platform execution and manual play were unavailable. No runtime dependency was added.

## Historical assessment

[Assessment](reports/assessment-summary.md) · [Capacity/economy details](reports/supplement.md) · [Independent director review](reports/director-review.md) · [Independent quant review](reports/quant-review.md)

All288 comparisons were freshly played on frozen SIM117 before renewable-wood master integration. They do not establish SIM120 effectiveness. The archived continuous-score analysis uses the then-required linear mean over observed time; it is historical, not a claim under newer trajectory-score tooling. All960 complete raw probability histories are retained for rescoring.

Only3/96 adaptive setups selected more than one pipeline; none selected4. The interrupted earlier cohort is excluded in full. No historical outcome substituted for a control.

## Download

[Review archive](maxima-prestige-review.tar.gz) (38,142,528 bytes, below the48MiB allowance) · [Archive identity](archive.json)

SHA256: `f522f3d213717af0368342ce4239e0c3393219362a070c3ab9cee838590d9cb1`.

The archive contains complete288 committed result records/configurations/histories, the frozen experiment, all96 transported map objects, six retained replays, one retained terminal save, five selected capacity/cancellation logs, source overlays, native/Python/build logs, checksums and the current active-Prepare save. Objects preserve their transport hashes and encoding metadata. The remaining full telemetry stays local; complete aggregate audits and hashes are included, with that limitation explicit. Generated artifacts are hosted on this dedicated evidence branch, not committed into the feature branch.

## Scoring update at PR head3c59e1a019

The PR now includes outcome-aware log-time scoring. [Scoring verification and adjusted cohort results](scoring/scoring-verification.md) supersede the historical linear-score interpretation.67 tournament tests pass; all960 full/compact scores and two actual saved continuations match. Gameplay source and its prior native validation are unchanged.
