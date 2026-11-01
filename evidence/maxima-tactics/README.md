# Maxima campaign and proximity verification

Tested commit: `ae6c748844f58fbfef8003f5e8ca3e08ef4b6c91`. Integration base: `738dd459f140addf4f817c13fc480c7363f0784a`.

Release build: Linux x86_64, GCC 13.3.0, full client, dev_fast=0, no PCH/unity. Native unit+engine cases: **583 passed, 1 display case skipped, zero failures**. Source-policy checks:59passed/2skipped. Maintained telemetry analyzer:14passed. SIM108 golden match and version contract pass. Save writer189, minimum save floor58, replay floor187 and network73.

The archive includes exact commands, build inputs, compiled provenance, JUnit, logs, source patch, golden record/trace and checksums. It also contains complete per-game analysis rows for the completed576/144/192-game studies, all96 exported maps, and the first baseline/ring40 pair in frozen placement-manifest order with saves, replay, raw warrior CSVs and logs. That pair is illustrative evidence, not the statistical argument.

Historical tournament results are from frozen older research sources. Full registered executable/asset bundles and the full raw corpus remain retained locally and are not in this archive. Old private research187/188 saves and replays require their matching bundles, identified in the included manifests; current official-format readers are not interchangeable. CSV/JSON analysis is directly inspectable. The576-game probability analysis interpolates missing saved-history segments; the192/144 analyses use full dense model curves. No simulation reruns were needed for the saved-data extraction.

The new flag proximity preference has correctness and continuation coverage, **not a measured tournament efficacy gain**. Other OS/architecture checksums, GPU execution and a maintainer play-through remain unverified. These limitations are preserved rather than implied by Linux test success.
