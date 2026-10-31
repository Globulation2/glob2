# Bulgarian localization verification evidence

Feature PR: https://github.com/Globulation2/glob2/pull/1317

This evidence branch contains development/review material, not shipped game files. Implementation remains draft pending native production captures and serial/threaded browser release verification. Current feature catalog identities are recorded in manifest.json and the review archive.

## Completed

- 3393 native and 2843 online source keys; all6236 have semantic, editorial and adversarial records (18708 per-key bindings). Archived identical source/target pairs are carried forward explicitly, while changed entries have actual final-value delta reviews.
- Native strict audit, post-integration10 translation tests, online37×2843 catalog/generated-output checks, SDL3_ttf font test, final172-character cmap coverage in both fonts.
- Google Cloud standard NMT:46 requests,219880 protected code points,4.39760 USD conservatively reserved;10 USD cap. Raw provider requests/responses exclude authorization headers; subsequent edits are stored separately. See provider-pricing-recheck.json for the final rate recheck, not an invoice claim.
-40 mocked drafting-tool tests; two launcher packaging tests; actual Caddy identity/gzip font and licence MIME test.
- Platform suite at77170ded9b:1174 passed,15 skipped. New initial-title API suite atb07193e1de:4 passed.
- Final web build and Bulgarian online checks atb07193e1de product sources:11 passed, one intentional duplicate layout-project skip;20 desktop/phone route cases inside the layout test. AI/map/music labels and canonical protocol values, initial translated draft names, stored authored-name preservation, actual loaded bundled font and explicit locale persistence pass. See final-online-qa/verified for exact commands, source/bundle/font hashes, test reports and screenshots.

## Known baseline failures

Browser unit suite:146 pass,3 fail because existing replay/studio fixtures carry version186 while the engine accepts187. All relevant test, fixture and version files exactly match master738dd459f140addf4f817c13fc480c7363f0784a; see browser-unit-master-comparison.json. They are preserved as failures, not counted as passed.

## Pending / limits

Native linked renderer tests/captures, both WebAssembly release variants and their actual startup/persistence checks remain pending in this snapshot. Requested affected/browser/Windows hosted verification is queued. Linux/Chromium desktop and resized phone captures do not certify native Windows/macOS/mobile-device rendering. Provider-backed AI generation, payment flows and native browser-owned file chooser translations are not established by the online fixture QA. Some populated gallery responses are explicit existing fixture mocks, identified in the report.

Translation is agent-reviewed, not native-human-certified. No production deployment is performed by this PR.
