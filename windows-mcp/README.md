Verification for PR #1201, commit 40658823f264208d8fa0eb4fb2d1eccdd1788726; base 329b7dcca61c792e0efa0d87418eb383d415eeb2. Master freshly fetched and unchanged before final verification.

Windows diagnostic run https://github.com/Globulation2/glob2/actions/runs/38864320252 reaches MCPTest.cpp after the Bastion Keys fix, then fails with Boost.Asio's `WinSock.h has already been included` error. The MCP test includes SDL/Windows UI headers before Boost networking. Move Boost.Asio and Beast ahead of those headers so Winsock 2 is established first. Only test include order changes.

Ubuntu 24.04 x86_64, GCC 13.3.0, pinned SDL 3.4.16, release=1 (-O3), Mesa Lavapipe and Xvfb. Exact flags and clean source identity are in provenance/build-provenance.json.

- `GLOB2_SDL3_PREFIX=<pinned-prefix> GLOB2_GLSLANG=<pinned-glslang> scons --build=build/ci-recovery-release release=1 -j6 engine-tests`: passed.
- `env -u DISPLAY VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/lvp_icd.json python3 test/run_tests.py --binary engine --build-dir build/ci-recovery-release --filter 'MCP/*' --fullscreen --display-jobs 3 -j6 --junit <tests.xml> --artifacts <tests>`: all 14 cases passed, zero failures/skips. Includes protocol validation, loopback transport, cancellation and software/OpenGL/SDL GPU capture/recording coexistence. JUnit, screenshots and recordings retained.
- `python3 test/check_sim_revision.py --base origin/master`: passed.

Actual Windows compilation is running at https://github.com/Globulation2/glob2/actions/runs/38865544922. Local validation covers Linux execution and the source-level include-order defect; it does not claim a completed Windows build. Full master CI remains the hosted recovery checkpoint. No production behavior, simulation version, saved data or generator output changes.
