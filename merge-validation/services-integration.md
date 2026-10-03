# Online services integration validation

Source 9fb540d50 integrates master 5c46e832b. Skin storage is passed explicitly
to online rooms/matches, following application service ownership. Skin migrations
0020–0028 follow the landed presence and warm-map migrations; upgrade from0019
retains an existing account. Native game/probes build;607 unit cases;29 focused
engine cases; real API lifecycle probe; platform lint/typecheck and367 tests
(5 existing skips);258 build contracts (3 environment skips) pass. Browser serial
and threaded builds/package pass; serial and threaded Chromium mesh/context-loss/deferred
asset tests2PASS each; Firefox and WebKit2PASS each.

Crowded-render180 checksum frames pass with488 added units and two paints.
Steady geometry/raster calls remain0. New timings are integration smoke evidence
on a busy software-rendering host, not a matched optimization comparison.

Windows SDL environment setter now synchronizes application CRT getenv; scoped
fixtures still restore distinct OS/CRT values. Hosted Windows verification pending.
Previous windowed TSan run reported a mutex lifetime race wholly in uninstrumented
Mesa llvmpipe workers (job111225659481). Windowed check uses LP_NUM_THREADS=0
with GLOB2_SIM_THREAD=1, plus isolated unavailable D-Bus addresses. No sanitizer
suppression added. Local300tick windowed game passes; hosted verification pending.

Ready-PR build37134771915 started after push;37134769765 is CI housekeeping.
and require current Relevant checks passed SUCCESS before merging PR628.
