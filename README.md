# PR767 paired replay acceleration verification

Tested final head e39c387828ff654b1165f0c5101e66003b138e0e, base 5ea6b1bc55bdd18889eecce0505112f34c4a8e48. Fresh master fetched before final verification remains the same base. Native release engine rebuilt at clean final head, including current AI Studio/master integration.

Hosted failure: master0d26564bc27d547740b9fbda1395df667d5f1bb8 run37265399142 Native Clang coverage job111623637807, source GameSpeedTest.cpp331. O0/coverage accelerated replay1117ms fails fixed800ms despite a hardware-dependent decode/draw/event workload. Exact retained failure in hosted-clang-failure.txt. Full hosted archive remains available; read only635787bytes of its8.79GB ZIP through HTTP ranges to inspect reports without downloading unrelated coverage data.

The test now requires Maximum and fast-forward each finish faster than normal playback of the same25-tick replay on this build/renderer. Normal timing still >=800ms, normal live timing still >=1500ms with Maximum faster, pause/hard-pause still200..3000ms, all seven engine/replay checksum checks retained. No production, gameplay, simulation, save/replay/network or version changes.

Environment and build flags in environment.txt and build.log: Ubuntu26.04.1 Linux x86_64 GCC15.2.0; pinned SDL3.4.16/SDL_image3.4.6/SDL_ttf3.2.2; release C++20 -O3 -s -fPIC with native SDL SDK RPATH; pinned encoder Pillow12.2.0/libwebp1.6.0. OpenGL uses Xvfb/Mesa software rendering.

Exact build:
CCACHE=1 GLOB2_SDL3_PREFIX=/home/bradley/.codex/worktrees/repair-tls-rejection-fixture/glob2/build/sdl3-ci/prefix GLOB2_ASSET_ENCODER_PYTHON=/home/bradley/.local/share/glob2/development/caches/asset-encoder-runtime-assets-v1-py3.14/venv/bin/python scons release=1 build/linux/client/release/test/glob2-engine-tests -j8
PASS exit0.

Exact normal verification, from PR checkout:
SDL_VIDEODRIVER=x11 GLOB2_TEST_SOURCE_ROOT=$PWD GLOB2_TEST_ARTIFACTS_ROOT=$PWD/artifacts/replay-speed/final LD_LIBRARY_PATH=/home/bradley/.codex/worktrees/repair-tls-rejection-fixture/glob2/build/sdl3-ci/prefix/lib xvfb-run -a build/linux/client/release/test/glob2-engine-tests --test-suite=GameSpeed --reporters=junit --out=artifacts/replay-speed/final.xml
PASS3cases229assertions0failures/errors exit0,6.38783s. Live normal1982ms/Maximum48ms, pauses357/288ms. Replay normal1005ms/Maximum21ms/fast-forward45ms.

Controlled overhead: gcc -shared -fPIC clock-cost.c -ldl -o clock-cost.so; LD_PRELOAD=$PWD/artifacts/replay-speed/clock-cost.so added to identical environment. The shim sleeps7ms when SDL_GetTicks is sampled, then returns the real SDL tick; it changes wall-clock work cost, not supplied timestamps or production speed settings. Restrict --test-case='live engine speed*'.
Old clean d62882a8da9dfad7075b50aa51d8c17cba5473c9 native binary: FAIL exact old elapsed<800 assertion,974ms (before-clock7.xml/log). That baseline includes the same speed/pacing production code; subsequent AI Studio changes do not alter this test's unconfigured native run.
Final e39c387: PASS1case22assertions0failures/errors exit0,13.7369s. Live normal2049ms/Maximum1880ms, pauses2132/2117ms. Replay normal1039ms/Maximum959ms/fast-forward950ms. All unchanged checks pass even though both accelerated runs exceed the old arbitrary ceiling.

Checksums: retained exact per-run engine checksums in both logs and checksums.json. Four live/pause states agree within each run; all three replay playback modes agree. These are the existing speed equivalence checks, not a cross-platform/per-tick determinism claim.

Coverage targets the complete changed speed suite and controlled reproduction of hardware-cost sensitivity. Local full Clang O0 coverage, Windows/macOS/browser and complete engine matrix not claimed; unchanged production code does not require a simulation revision. Author accepts this focused verification under AGENTS.md; hosted cheap contracts pass, full master confirmation follows asynchronously.
