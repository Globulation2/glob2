# Reviewed CPU Music Studio verification

Tested source: `501906b2051b148a9303ec40f847aa8a98c2d8c0`.
Integrated base: `3607e90980ad42766b6d5663ac41e1a2b93e0f93`.
Latest fetched master: `0d26564bc27d547740b9fbda1395df667d5f1bb8`.
Later master changes cover translations and Windows CPython encoder setup; inspection found no conflict requiring a merge or repeat of Linux integration validation.

This evidence supersedes the earlier source verification. Earlier failed synth candidates remain in the parent evidence commit, `2b4694bc189ab30290ca3d71bcc2f9e59b7e9285`, for repair history.

## Environment and scope

Ubuntu 26.04.1, Linux 7.0.0-31-generic x86_64; Node 22.22.1 for ordinary npm checks and Node 24.19.0 for native TypeScript/browser/render commands; Python 3.12.14; PostgreSQL 16 test instance on port 55432; Docker 29.1.3. Dependencies use the committed npm lockfile, music Python requirements and pinned sample manifest. No native simulation compilation or build flags apply to these authoring-service changes.

Final optional worker image: `glob2-ai-music-worker:reviewed`, image ID `sha256:39b8c9b86105851e98feed934b8c75ab902f7b0beb01e4b7f57f65edde28b14b`.

## Results

| Check | Result | Evidence |
| --- | --- | --- |
| Focused platform integration/unit tests | 122 passed, 2 optional sandbox/render skips | [log](logs/music-studio-final-reviewed-platform.log) |
| TypeScript type checking and lint/format | passed | [typecheck](logs/music-studio-final-reviewed-typecheck.log), [lint](logs/music-studio-final-reviewed-lint.log) |
| Python music regression suite | 135 total, 4 optional skips | [log](logs/music-studio-final-reviewed-python.log) |
| CI policy tests | 87 passed | [log](logs/music-studio-final-reviewed-ci.log) |
| Deployment driver tests | 13 passed; earlier transient failure did not recur | [log](logs/music-studio-final-reviewed-deployment.log) |
| Web build and desktop/phone browser tests | build passed, 2 browser tests passed | [build](logs/music-studio-final-reviewed-web.log), [browser](logs/music-studio-final-reviewed-browser.log), [screenshots](visuals) |
| Real non-root namespace adversarial tests | 2 passed, optional render skipped here and exercised separately below | [log](logs/music-studio-review-sandbox.log) |
| Final worker image and offline assets | build passed; assets ready | [build](logs/music-studio-final-reviewed-image.log), [probe](logs/music-studio-final-reviewed-container-assets.log) |
| Default container isolation policy | expected fail-closed rejection of namespace creation | [log](logs/music-studio-final-reviewed-container-isolation.log) |
| Fresh acoustic and revised synth renders | both pass all mandatory QA, warnings retained | [acoustic](renders/acoustic-final/report.json), [synth](renders/synth-revision-2/report.json), [ZIP audit](logs/packaging-audit.log) |

Real renders used the final render/runner sources. The final subsequent provider-budget edit does not change audio computation. Acoustic: 159.990 seconds elapsed, 158.354 CPU seconds, 7,966,772 KiB peak process RSS. Synth: 274.388 seconds elapsed, 263.479 CPU seconds, 3,778,684 KiB peak process RSS. RSS is per process, not aggregate memory. Both ran under a 12 GiB cgroup with bounded scratch. Full resources and individual score/audio findings are retained in their reports.

[Acoustic preview](renders/acoustic-final/preview.opus) · [Acoustic download](renders/acoustic-final/set.zip) · [Synth preview](renders/synth-revision-2/preview.opus) · [Synth download](renders/synth-revision-2/set.zip).

## Review and changes

Three subagents reviewed service/billing/privacy, worker/isolation/recovery, and UI/playback. The author independently reviewed their findings and changes. Two review/fix passes included cross-review between service and worker reviewers, and a final check of daily operator budgets. No actionable review findings remain.

- Journal candidate allocation before execution so crashes cannot reset the three-cycle budget; persist candidate checkpoints and completed artifacts.
- Recover successful and rejected provider results after lost COMMIT acknowledgements; keep unknown outcomes pending, reject mismatched known replay inputs, fence late workers, and preserve exactly-once settlement/refunds.
- Apply lowered operator daily limits to previously queued requests; include worker/prompt/isolation inputs in source fingerprints.
- Seed recipes before import/arrangement; require aggregate cgroup memory bounds, bounded regular reports and output files, unique complete independent checks, and trusted final metadata.
- Align comparison validation with the audible revision, prevent stale candidate preview playback, clear only deleted history's browser drafts, and share validation detail rendering.
- Move SSE transport to the shared HTTP layer and extract shared protocol schemas without changing serialized Map Studio contracts.
- Update music/deployment/architecture documentation and explanatory comments around recovery, trust boundaries and budgets.

Coverage targets database upgrades, product isolation, concurrent/idempotent settlement, refunds/webhook replay, lease replacement and uncertainty, cancellation/deletion, private release publication, artifact cleanup, bounded recovery, adversarial recipe isolation, existing local recipes/community conversion, and UI history/reconnect/checkout/validation/player behavior. Browser screenshots use deterministic mocked audio; real audio validation is separate above.

## Qualification limits

Generation and sales remain disabled by default. There were zero live model calls and no external Stripe transactions. These are local recipes and a manually targeted synth revision, not real-LLM qualification. Real-model initial/revised sets, provider cost measurement, human listening, operator pricing and production namespace policy remain required before enabling the service. Automated QA does not establish subjective musical quality.

The default Docker policy rejects nested namespaces and accepts no jobs. A reviewed deployment policy or a suitably isolated Linux host is needed; no privileged/unconfined bypass was used. No Windows/macOS/Android native engine or simulation checksum tests were run: authoring services/audio artifacts changed, with no simulation, save or replay format changes and no simulation revision bump.

Exact commands are in [commands](commands). SHA256SUMS covers every evidence file except itself.
