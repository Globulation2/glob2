# Thai localization evidence

Source revision: `03abf217c082d97f1512b2ac691ef64d3e30b065`.

[Browse exact committed implementation](https://github.com/Globulation2/glob2/commit/03abf217c082d97f1512b2ac691ef64d3e30b065).

This dedicated evidence snapshot contains frozen sources/catalogs, actual Google responses and budget ledger, independent agent review history (including exact-hash carry dependencies), focused checks, and available screenshots. Review the manifest for exact file hashes and collection scope.

Collection status is provisional until the PR release gates and platform visual reviews are complete. Logs are losslessly gzip-compressed. SDKs, binaries, application/source archives, private fixture cookies and profiles are excluded.

Approval records report agent review, not native-human certification.

- [Source-current successor and carry proof](successor/successor-proof.json)
- [Actual per-batch role approvals](successor/checks/independent-final-approvals.json)
- [Current release-gate status](successor/checks/status.json)
- [Language brief and terminology](campaign/language-brief.json)
- [Review history and immutable carry dependencies](campaign/reviews)
- [Actual Google requests/responses](campaign/google/th/requests) and [budget ledger](campaign/google/th/ledger.json)
- [Focused checks](campaign/checks) and [rendering evidence](campaign/rendering)
- [Browser report](campaign/browser-qa/report.json) and [capture provenance](campaign/browser-qa/provenance.json) (if included)

Capture scope is explicit: the original28-view collection remains interim;20 e100 studio views cover unchanged studio components; [Two fresh username-hint SSR views](campaign/browser-qa/username-hint-final-report.json) cover the revised auth text. [Native input equality](successor/checks/native-rendered-input-equivalence.json) permits unchanged3e5 native capture evidence only. Inspect each report and actual build provenance; pending platform gates remain open.

This partial snapshot closes desktop/phone rendering only:58 focused native tests,36 PNGs and360 matrix measurements. Their actual producer is a Linux development/software build, not a release-platform certification. Platform/Caddy container QA passed; engine-container and mobile/release-platform work remain pending.

- [fresh username hint SSR](campaign/browser-qa/username-hint-final-report.json)
- [unchanged native rendered inputs](successor/checks/native-rendered-input-equivalence.json)
- [display scope inventory](campaign/reviews/display-scope-audit.json)
- [exact source and target approvals](successor/checks/independent-final-approvals.json)
- [prior username approval history](successor/reviews/pre-username)
- [native development verification](campaign/rendering/final-native-verification.json)
- [native actual PNGs](platform/thai-localization__native-final/manifest.json)
- [platform and Caddy container QA](platform/thai-localization-final__container-qa/summary.json)
- [iOS second failure and bundle pass](platform/thai-localization__platform__ios-b6e-bounded/result.json)
- [Android first failure](platform/thai-localization__platform__android-e100-runtime__mobile-smoke/result.json)
- [latest QA only source equivalence](successor/checks/qualification-only-head-equivalence-03ab.json)
- [actual Android release emulator review](platform/thai-localization__platform__android-cedc-bounded/visual-review.json)
- [latest master fixture reproduction](platform/thai-localization__checks__master-baseline-80596/provenance.json)
- [latest master exact source hashes](platform/thai-localization__checks__master-baseline-80596/source-hashes.json)
