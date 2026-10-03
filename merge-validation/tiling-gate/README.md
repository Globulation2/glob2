# Local gate after map-repetition integration

Source: e45b2bdfa87f7fcce6d13691b6639a9cb4d16528; master ce56e2078.
The maintainer authorizes a merge on passing relevant local tests despite hosted
CI problems. Hosted checks are not represented as passing.

The native release game, test harnesses, preview and online probes build.
All 651 unit cases pass. All 64 selected engine cases pass with no skips,
including map repetition, save/load, golden per-tick checksums, multiplayer,
recording, Scene diagnostics, settings and online services. The first engine
run had 63 passes and one offscreen-restoration timeout under the display
runner. That case passed alone on plain Xvfb in 3.6 seconds. A complete rerun
with LP_NUM_THREADS=2 then passed all 64 cases on the original runner setup.
The timeout, isolated retry and full rerun logs are retained. Product code and
assertions were unchanged; the timeout's root cause is not established.

Four 180-frame classic/skinned comparisons pass: normal, overview, 5x zoom and
normal with adaptive detail disabled. Every draw preserves simulation state;
classic and skinned states match per frame. Warm geometry and raster counts
are zero; overview skin composites are zero. Normal and detail-disabled modes
perform 73,556 skin composites. Classic captures are pixel-identical to the
Scene integration in all four configurations. Skinned captures are identical
except normal, where 82 of 480,000 pixels differ by at most 20/255. The normal
capture was visually inspected. The software-renderer thread count differs
from the preceding integration; timings under concurrent compilation are smoke
evidence only, not new matched profiling results.

Live API lifecycle passes. Build contracts: 288 cases, three environment-dependent
skips, no failures. Five translation contracts pass, and the runtime-table audit
reports zero structural errors. Platform source is unchanged from 3de565d65:
438 tests with six existing skips, lint/typecheck, six designer cases, four
full-page accessibility combinations and the production web build are recorded
in the adjacent studio-gate evidence.

The serial/threaded browser package builds and all ten selected browser cases
pass: live skins and lazy assets in Chromium serial/threaded, Firefox and
WebKit, plus serial/threaded 1,500-tick pacing/checksum and paused-input tests.
The threaded pacing retry passes with the original time limit and assertions.
Signed online replay is covered by the preceding Scene gate; the final fog
integration will repeat it against the rebuilt runtime. These results belong
to the map-repetition revision, not the subsequent smooth-fog integration.

Replays are losslessly gzip-compressed; BMP images are converted to PNG.
Commands, match records, verifier verdicts and checksum traces are attached.
Hardware GPU and Windows/macOS checks are unavailable locally. Actual Stripe
checkout/fulfillment/refund remains unverified without account credentials and
price IDs; this is separate from the tested local Stripe adapter contracts.
