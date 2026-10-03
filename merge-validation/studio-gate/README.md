# Local gate after Hive Mind and Map Studio integration

Source: 3de565d65, integrating master 3092bdeda. The follow-up removes one
duplicate test import. The maintainer authorized merging on passing relevant
local tests despite hosted CI problems. Previous local-gate evidence is for
aac206307 and is not represented as this revision's result.

Platform lint/typecheck and 438 tests pass (6 existing skips). The retained first
check log shows the duplicate import corrected before the complete rerun.
Skin migrations 0023–0031 follow master's 0022; tests cover an existing 0022 database
and account preservation, complete typed schema, account exports and retained
blobs across skins, Hive Mind and Map Studio.

Six desktop/phone designer tests and four full-page theme/device accessibility
cases pass; the production web application builds. Build contracts: 288 tests
pass with 3 environment-dependent skips. Translation structural errors: zero.

The native release game, tools and test harnesses build successfully. All 651
unit cases pass. The first engine run completed all 50 cases successfully, then
the outer Xvfb wrapper terminated with status 143 before the next script stage.
The complete 50-case engine run was repeated with the runner's isolated displays
and exited cleanly. It covers settings, Hive Mind, online services, multiplayer
recovery, recording and replay checks. The resumed script then passed three
180-frame classic/skinned state comparisons and the live API lifecycle test.

Render comparisons cover normal, overview (clamped to 0.15625), and 5x zoom.
Drawing leaves simulation state unchanged; classic/skinned checksums match on
all 180 frames per zoom. Warm geometry/raster work is zero; overview performs no
skin composites either. All classic captures are pixel-identical to the previous
integration. Skinned overview/high captures are identical; normal differs in
9 of 480,000 pixels by at most 1/255. The normal capture was visually inspected.
Timings were collected on a heavily loaded shared machine and are smoke evidence,
not matched performance measurements or new-base speedup claims.

Engine artifacts include match records, verifier verdicts and per-tick checksums.
Replays are losslessly gzip-compressed; decompress before game playback. BMPs are
losslessly converted to PNG. Commands and logs are retained alongside evidence.

Browser build and cross-browser results will be added when complete.
