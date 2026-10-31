# Filipino localization review evidence

Source commit: `03fcf10e9f48b29aaee4d02e6160a6e0449aefb2`. Base: `e67cba82e7ff1738efdfc65bf3a7c47dc8f7dcd1`.

The archive contains frozen sources, Google request/response receipts, author edits, independent meaning and editorial reviews, an adversarial pass, exact per-entry English-retention rationales, and their hash-bound provenance. All 6,229 keys and all online plural branches were reviewed. This is agent review, not native-human certification.

Google NMT: 60 successful requests, 231,771 conservatively counted input characters, US$4.63542 estimated before credits. Historical request payloads reconstructed later are explicitly labeled as reconstructed and verified against the original attempt hashes.

Catalog SHA-256:

- Native: `5c86dff4c8e9fb03c89bae1185740767dfc56959003e55e81c3bb84479531f61`
- Online: `99ed554f753390682d437318e20ed9d7ddbd54fbe2c798df41b42d4fb3fb2446`

Archive SHA-256: `f4353030838d2d2acf290d68e9dd61d97235a470fef39442d3273c4c13d2f8fd`.

Translation contracts and bundled-font coverage passed. Rendering, real-browser startup, and platform verification are still in progress; pending gates are retained as pending in the review record. Additional validation evidence will be added before the PR becomes ready.

Extract the archive and inspect `kit/reviews/fil.json`, `kit/reviews/fil-provenance.json`, and the original per-stage evidence. Temporary helper scripts are included only as review evidence.

## Website validation

Source commit: `091b6ef5120fc0d6aa37ac9570e2cc61c9f878c2`. `filipino-web-validation.tar.gz` contains eight reviewed English/Filipino screenshots, pre-fix examples, six passing Chromium website tests, 40 passing i18n tests, type checks, and asset contracts. Long Home labels now wrap inside their three-card strip and narrow section headings use the available width. Native game and WebAssembly game captures remain in progress.

The full browser unit suite passed 147 tests; its three replay/Studio fixture version failures reproduce on identical unchanged baseline inputs. The archive includes that baseline reproduction.

Web archive SHA-256: `9f2b98e5c91a55cfa1bbb0df86c663d7fa7b5180cfaf2696575ad4fb1402789c`.
