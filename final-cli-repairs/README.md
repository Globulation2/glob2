# Final resource-refactor CLI repair evidence

Production binary: frozen b3fab1a48 (SHA in each execution manifest). Final source repair revision: f264 (full SHA in bundle manifest). Repairs change fixtures/checker selection, translations and test reporting, not simulation production.

- JavaScript: existing resource-era fixtures were already correct; Python selected historical terrain files. Both profiles pass complete 256-tick traces, exact one/four-worker replay/save output and six saved continuations. Hosted GCC11 and Windows profile1 complete trace equals the committed resource golden. All historical entity records are unchanged.
- Released telemetry: resource-era references preserve old terrain references. Three legacy fresh loads and v108 checkpoint match in serial/four-worker modes. These pin the approved resource/AI policy epoch, not trajectory identity with Sim20. Classification is in the separate gameplay evidence bundle.
- Maxima: new resource baseline preserves the old terrain JSON. Both worker modes match 512 complete tick hashes; save/reload at30256 matches256 remaining complete records, allowing only documented header-version contribution.
- Translations: five tests passed, strict catalog audit zero structural errors; font coverage passed. Existing pending translations remain explicitly pending.
- Generator table:544 Linux rows verified locally; independently collected GCC11/GCC13 rows agree.512 hash changes,32 unchanged,zero status changes. All1108 historical rows preserved. Full macOS resource-epoch hashes remain unverified.
- macOS reporting: hosted842 engine cases passed,1 error resulted solely from deliberate WARN(false) coverage notices entering JUnit. Commit231f4a1e5 replaces those notices with explicit RESOURCE_DESIGN_UNVERIFIED stdout; portable topology assertions are unchanged. This reporting repair has source review; no new macOS run is claimed.

Exact engine argv, output traces, saves and execution manifests are retained below. Checkers are included from the repaired source. No performance claims. Hosted run37546676815: https://github.com/Globulation2/glob2/actions/runs/37546676815


## Publication supplement

Bundle SHA256: `91a6bc8fb1bba8f613e411d6ded483902c31d6ce5622521507551e0d93d5fd96`. The adjacent `merge-generator-reporting.log`, JUnit XML and inventory retain the final targeted generator check: 1 passed, 0 failed, 0 skipped. `sha256.json` identifies the bundle and supplement bytes.

`telemetry-epoch-classification.zip` supplies the actual pure/approved/9a/SIM23/final comparison artifacts referenced above: pure f0 reproduces the old gd-large reference, 9a matches approved-fix entities, and SIM23 matches final b3 entity/team records for all four retained scenarios. Aggregate bytes retain epoch differences; the checker compares the complete new sidecars.
