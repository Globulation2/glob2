# Local gate after Scene renderer and pacing integration

Source: 0568fce2001c1b2ada9bcee4a148e0e5f1ab1f66, integrating master d46638d85.
The maintainer explicitly authorized merging on relevant local tests while
hosted CI is having problems. Hosted results are not claimed as passing.

The native release build succeeds. All 651 unit and 57 selected engine cases
pass with no skips. The first unit run timed out in the portable sprite batch
case using the runner-owned display. An isolated retry on plain outer Xvfb
passed, then the entire 651-case suite passed on that display setup. No test
assertions or product code were changed to obtain the pass. The first log,
retry and complete rerun are retained; the timeout's root cause is unconfirmed.
The engine suite uses runner-owned isolated displays and exits cleanly.

Four classic/skinned rendering comparisons pass: normal, overview (0.15625),
5x zoom, and normal with adaptive detail disabled. Each verifies unchanged
simulation state during rendering and identical classic/skinned state for
180 frames. All steady-state geometry/raster uploads are zero. Overview also
performs no skin composites. Normal and adaptive-detail-off runs both perform
73,556 skin composites over 148 measured frames. The adaptive-detail-off
capture was visually inspected for painted units, swarm and building color.
Classic captures are pixel-identical to the prior integration at all three
zoom levels. Skinned overview/high captures are identical; normal differs in
31 of 480,000 pixels by at most 1/255. Adaptive detail off is a separate visual
configuration and is not expected to match the adaptive-detail-on captures.
Timings are smoke evidence under concurrent builds on a shared host, not a
new matched performance measurement. Original profiling remains elsewhere.

The live API lifecycle probe passes. Build contracts pass 288 tests with three
environment-dependent skips. Platform source inputs are unchanged from
3de565d65: the adjacent studio-gate evidence supplies platform lint/typecheck,
438 tests (six existing skips), six designer cases, four full-page accessibility
combinations, production web build and translation structural validation.
Unit/engine sources built at 97a11b73d are identical to this head; the only
subsequent code change adds adaptive-detail control to the preview tool, which
was rebuilt and exercised in all four comparisons.

Browser results are pending and will be added before merging.

Engine artifacts include match records, verdicts, per-tick checksums and
recordings. Replays are losslessly gzip-compressed; BMP captures are converted
losslessly to PNG. Commands, logs and captures accompany this evidence.
Windows/macOS and hardware GPU validation are unavailable locally. Actual
Stripe checkout/fulfillment/refund still needs account credentials and prices.
