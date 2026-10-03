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
