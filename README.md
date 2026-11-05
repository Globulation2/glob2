Evidence for Globulation2/glob2#1440.

- Tested PR head: `60778f38682bb86b73580c68b743956c4e3bf80e`
- Baseline and integration base: `cc0bbe1bd19ad996fafa13256daae9c1f8472613`
- Host: Apple M3, arm64, macOS 27.2 (26B123), Apple Clang 21.0.0
- Build: `python3 tools/dev_build.py --build=artifacts/zoom-stall/build torus-render-benchmark CXXFLAGS="-I$(brew --prefix openssl@3)/include" LINKFLAGS="-L$(brew --prefix openssl@3)/lib"`. Both builds used `dev_fast=1` and identical SDL dependencies, compiler and flags. Engine cases were compiled with the same client objects using a local focused test registry.
- Workload: Honeycomb Isle, 512 × 512, seed 713, four teams, frozen at tick zero. Each route has three warm-up frames and 48 measured frames. Both routes run presentation animation, Legacy OpenGL, HD artwork and adaptive detail, with a 1280 × 800 window at Retina scale. The timed frame ends at GPU completion. The candidate includes asset polling in the timed interval. The harness excludes the final buffer swap and live simulation.
- Reproduction: from a checkout of the corresponding revision, use `bash run.sh /path/to/torus-render-benchmark /path/to/honeycomb-tick-zero.game.gz /path/to/existing-parent/run fit`. Use `motion` for the scripted zoom and pan.
- The final captures matched byte for byte between baseline and candidate for each route. The PNGs show the candidate. All four runs retained simulation checksum `7a0465db`.
- GL texture bytes at the end of the fit route were identical. The motion route ended with 6,274,432 fewer tracked bytes.
- Coverage: macOS Legacy OpenGL timings, native display regressions for terrain, editor and resource readiness, and CPU mip arithmetic. No live display cadence, release timing, or physical-device performance claim.
