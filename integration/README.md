# Current master integration

Native sources were rebuilt on d8d2cd641 plus the replayed skin commits and
integration fixes committed as d13c6cf6d. 611 unit cases, 37 focused engine
cases and 180 frame-by-frame rendering checksum checks passed. The renderer
capture is a stress fixture, not a gameplay map. Hardware is shared llvmpipe,
so these unmatched timings are correctness evidence rather than a new speedup
claim. Subsequent e22085016 merges only upstream web accessibility fixtures.

362 platform cases passed, with five explicit skips; lint/typecheck passed.
257 build contracts passed with three environment skips, using the pinned
asset encoder Python. 38 browser JavaScript tests and 6 desktop/phone designer
cases passed. Shared signature validation passed 20 positive/negative cases
each in Chromium, Firefox and WebKit (zero outstanding async entries).

Initial integration failures were fixed: duplicated relay event consumption,
new shared rate-limit API, migration ordering and shared/browser code boundary.
Retention/deletion/GC regression coverage was added. The new accessibility
sweep exposed a swatch-label issue; its correction and final results follow.

Both final serial/threaded browser builds and their two skin runtime tests pass
at 7c6df8d7a (render/hide/context restoration and no unskinned mesh download).
The focused repair from PR630 is integrated as cf3aada7c; both online probes
link and all seven runtime-package tests pass. Native client lifecycle against
a real isolated API passes after updating its integration helper for the
current refresh grace and browser confirmation-code flow (8458d92fe).
The distro Node lacks compiled-in TypeScript support, so this local probe test
uses the already-installed tsx loader through NODE_OPTIONS.
