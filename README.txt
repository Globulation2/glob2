Rendering overview evidence

Source PR: codex/render-overview-performance
Baseline rendering sources: 777d19e09
GPU: Apple M3. Host had substantial concurrent CPU/GPU contention.
CPU medians are process CPU, not idle-device FPS. No physical-phone qualification.

Scenario: symmetric arena generator 15, map seed 42, 256x256, 8 teams.
Game seed 19, eight Maxima players. Retained tick-20000 checkpoint naturally has
768 units. Add units to 3000, advance all eight AIs for 100 ticks, render tick20100:
2518 total units, 2441 map units; DRAW_WHOLE_MAP and health/food bars enabled.
Camera fixed full-map zoom 600/8192. Sweep repeats eight zoom levels and pans
across wrap seams for 120 frames without clouds +120 with clouds.

ai-comparison.json contains exact commands, environment, checkpoint/binary hashes,
CPU and wall medians/p95, draw counts, textures and checksums. Screenshots are the
full-map scene with clouds. Pixel parity for unchanged artwork is retained in
bars-fixed-pixel-check.txt; rectangle pixel tests also compare fractional zoom,
clipping, overlaps, outlines, transparency and the 4096-quad boundary.

Reproduce from the source PR checkout with a release native OpenGL benchmark:
1. Copy these evidence files under artifacts/render-profile (preserve ai-match-eight/).
2. Build: scons release=1 server=0 torus-render-benchmark
3. Run build-reference.py (change git show HEAD to baseline commit 777d19e09
   if reproducing after merge; baseline sources must exclude these optimizations).
4. Run run-ai-comparison.py. It uses an isolated profile and -g -F -m -s 1024x600.
The historical cpu-build.log supplies matching compiler/linker flags to the helper.
Use your current build's flags on another platform. Environment settings and
absolute paths in historical JSON/logs describe the original machine; the Python
runner resolves its current checkout paths.

Review feedback resolved: error propagation/unwinding cleanup, conditional GL
registration/context verification, synthetic-only placement constraints, and
native cloud detail respected during camera sweeps. Additional fake-backend tests
exercise destructor, capacity and body errors with subsequent scope reuse.
Follow-up review reported no remaining blockers (same-author review, not an
independent maintainer approval).

The final post-review test logs are appended to this branch after validation.
