# Marathi localization evidence

Validation head: `ba890db53e67617fb3cb1033c157ca578781196e`. Native/browser runtime producer: clean `671894f2a43a677e15a71cb4d45c81b758943f1a`. The follow-up changes only an Apple test's optional-SCons import; exact unchanged-production proofs are retained. Initial Chromium online captures identify `c9d772635398c57582069b58208eb337cee2787b`; the online UI sources are unchanged afterward.

This is an evidence branch, not a source or release branch. Reviews are by independent agents, not native-human certification.

* Complete Marathi: 3,393 game keys and 2,836 online keys (6,256 text/plural variants altogether).
* Google NMT used `bradley-personal-20261027` (Bradley Personal). All 87 reserved requests total 193,615 submitted code points, conservatively US$3.8723 before credits at US$20/million, under the US$9 ceiling. This is a request-ledger estimate, not a billing invoice. Raw responses, failed-token diagnostics and review/correction history are retained.
* Three independent agents completed semantic, editorial and adversarial reviews of every key, followed by correction rechecks. Source/catalog/batch hashes and explicit unchanged-value carry proofs are included. The Android startup string and both distinct Apple permission descriptions have separate three-agent final reviews.
* Native final SHA: `8b3f9e4972ee0eb1c3433110a7f0e83c9dc2fb7ea5f494b6925afa373c4fef97`. Online final SHA: `0a35ecc3e76fd00b68db89ca5dd18967304524eb7c3c0b8cc81c37b70e5694a4`.

## Completed execution evidence

* Native: all 34 capture jobs passed across 1280×900 and 390×844. Both complete font matrices cover 75,072 string/font/scale samples and 83,292 wrapped lines, with zero missing glyphs and zero production draw-bound failures. All 590 raw bitmap-width diagnostics remain available; each full bitmap fits its actual production drawing destination. Selected flagged pages were inspected without visible cropping. See `native-render-clean671/summary.json` and both full matrix reports.
* Ten menu/lobby screens and 57 gameplay/tutorial images were captured. The agent inspected 34 actual images, including every compact initial tutorial guidance image; root's additional inspections are separately recorded. Captures cover all ten tutorial starts per viewport, available automatic briefings and dialogs, not full lesson playthroughs. All 34 native overlays have exactly the committed catalog's 3,393 parsed key/value pairs. Preview overlays and compact presentation on Linux are not independent registered-package or Android/iOS runtime claims.
* Fifteen focused registered FontKerning/SystemLanguage cases passed. The unrelated full native engine aggregate was not completed. The original harness and executed stripped copy have identical allocated ELF sections/program headers/entry; exact hashes and stripping proof are retained. These are development rendering checks, not release or performance certification.
* Online app: six Chromium desktop/phone cases passed across the original run and focused capture retry; a transient Chromium screenshot-protocol failure is retained. A fresh production build at 671 passed eight more Firefox/WebKit Marathi home/switching cases at desktop/phone widths. They check script-font loading, selector fit, persistence, RTL-to-LTR switching, retained input and theme accessibility. WebKit on Linux is not a Safari device claim.
* Strict native/online inventories, formatting/plural contracts, generated browser runtime, full platform type checking and web builds pass. Drafting helper: 10 tests; translation review tooling: 13; native translation regressions: 10; platform/API localization: 33. Existing command/results manifests retain exact scopes and revisions.
* Apple resource contracts: four pass with SCons; dependency-isolated Python runs three and skips only the SCons-dependent case. The initial hosted selector import failure is retained and fixed by the test-only follow-up. Twenty-seven GOG source-guard tests pass. Apple tests execute on Linux, not Apple devices.
* Browser packaging contracts: 27 pass, one skip. Browser unit suite: 147/150 pass; three replay-fixture/version failures reproduce on the exact unmodified feature base `ed4ac0901f85a8e13d11b741ad3dea1f0a31df54`. They are not attributed to Marathi or asserted against later master revisions.

## Pending package/runtime gates

Both release WASM variants and their 12 actual browser-game runtime cases, and the final Android release package/emulator validation, are still running. Their completion is not claimed by this checkpoint. The translation-program gate record will be finalized after those results. macOS, Windows and iOS runtime execution remains unverified. Current master `80596f1477a1e3736d3b60287a62b6702e13c546` adds unrelated Maxima/simulation changes; a clean merge preview and unchanged localization/rendering-input scope are recorded.

## Inventory and review limits

Every quantitative matrix report is retained. To bound publication size, matrix PNGs are sampled deterministically: first/middle/last per font and scale, plus every explicitly inspected page. All actual menu/game/tutorial/browser screenshots are retained without that sampling. `matrix-image-inventory.json` records every generated matrix image's hash and whether it is published; other full PNGs remain in local task artifacts. This does not truncate measurements or count abandoned attempts as successful. Large JSON reports, if needed, are losslessly gzip-compressed with original hashes recorded in `published-inventory.json`.

Dependencies, SDKs, executable binaries, environment dumps and full external reference snapshots are excluded. Language references retain links, brief consulted excerpts and snapshot hashes. Historical review files identify earlier stages; final signoffs identify the approved bytes. Original unstripped/short-deadline partial capture attempts are explicitly abandoned and excluded from the 34 successful jobs.
