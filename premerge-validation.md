Pre-merge local validation and CI exception

PR #646 revision: c7b5c6a0893c00f3414c0d87a061d8466d969819.

The maintainer explicitly authorized merging on passing relevant local tests without waiting for the slow hosted CI queue. The final local rerun passed all 584 selected headless unit cases (12 display cases skipped) and all 36 selected affected engine cases. Engine selection covers gradient invalidation, convergent influence, Cortex, Maxima fruit routes/strategy and the committed golden match; the simulation-version check passes against current master. Earlier integrated validation covers seven exact full-match/reload checksum/order/save comparisons.

Current master was fetched. Subsequent base edits add rendering/HUD tests and source inventory entries plus a deployment-only CI selector exemption; no AI or gradient changes or merge conflicts were found. The locally compiled integration base remains 72f16b1de; these reruns validate the PR head, not a rebuild of the newer render/UI base changes.

Hosted ready-PR checks remain pending, with no reported failures at merge preparation. Full cross-platform simulation and strict performance confidence coverage remain the recorded limits. The maintainer's explicit exception applies to the required hosted merge gate.
