# Current partial release evidence

Actual qualification source: `667b2624f356a8c3f0d005535f54ffb0e46b6619`. Only language brief, pilot process and structural gates are closed. Six rendering/browser/platform gates remain pending. Historical producer revisions retain their original scope.

- [Current 46-batch role approvals](platform/thai-localization-release/reviews/th.json)
- [Current six pending gates](platform/thai-localization-release/checks/status-three-cheap-gates.json)
- [156 derived per-test cross-browser results](campaign/browser-qa/crossbrowser-tests-derived-results.json)
- [Cross-browser producer provenance](campaign/browser-qa/crossbrowser-test-provenance.json)
- [Snapshot manifest, exact hashes and whole-file omissions](manifest.json)

The original 271 MB Playwright report remains local. The explicitly derived report preserves every test result, duration, retry, error, skip annotation, browser and viewport; embedded image bodies are replaced by decoded hashes/sizes. Separate actual Thai visual captures are included. No native/WASM release qualification is inferred from online captures.

Earlier successor links below describe historical evidence only. The current release kit above has 3413 native and 2852 online entries.

# Thai localization evidence

Source revision: `667b2624f356a8c3f0d005535f54ffb0e46b6619`.

[Browse exact committed implementation](https://github.com/Globulation2/glob2/commit/667b2624f356a8c3f0d005535f54ffb0e46b6619).

This dedicated evidence snapshot contains frozen sources/catalogs, actual Google responses and budget ledger, independent agent review history (including exact-hash carry dependencies), focused checks, and available screenshots. Review the manifest for exact file hashes and collection scope.

Collection status is provisional until the PR release gates and platform visual reviews are complete. Logs are losslessly gzip-compressed. SDKs, binaries, application/source archives, private fixture cookies and profiles are excluded.

Approval records report agent review, not native-human certification.

- [Historical successor and carry proof (source-current at its original revision)](successor/successor-proof.json)
- [Historical per-batch role approvals](successor/checks/independent-final-approvals.json)
- [Historical release-gate status](successor/checks/status.json)
- [Language brief and terminology](campaign/language-brief.json)
- [Review history and immutable carry dependencies](campaign/reviews)
- [Actual Google requests/responses](campaign/google/th/requests) and [budget ledger](campaign/google/th/ledger.json)
- [Focused checks](campaign/checks) and [rendering evidence](campaign/rendering)
- [Browser report](campaign/browser-qa/report.json) and [capture provenance](campaign/browser-qa/provenance.json) (if included)

Historical capture scope: the original28-view collection remains interim;20 e100 studio views cover unchanged studio components; [Two fresh username-hint SSR views](campaign/browser-qa/username-hint-final-report.json) cover the revised auth text. [Historical native input equality](successor/checks/native-rendered-input-equivalence.json) documents the earlier3e5 rendered inputs only. It does not qualify the current3413-entry native catalog or its20 new entries. Inspect each report and actual build provenance; pending platform gates remain open.
