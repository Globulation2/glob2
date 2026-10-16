Local recovery checkpoint

Current merged master: 17e249a2735e3d76e76ca7e500b6ea872f4393e0 (PRs #1198–#1206). Full latest-master hosted run: https://github.com/Globulation2/glob2/actions/runs/38883032605 . It is pending behind active full run https://github.com/Globulation2/glob2/actions/runs/38878240431 at prior source a5b380584. No active master run was cancelled; superseded pending runs were replaced by GitHub's configured concurrency policy.

The complete local native run at clean 61cd188ecf8d50b229e21539bb9e62cf12f745e4 (tree identical to merged a3f118590f78dc653d7a2fb1d444a5b6d0dd9441) passes all 1872 jobs/2968 cases, zero failures/errors/skips. Command:

env -u DISPLAY VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/lvp_icd.json python3 test/run_tests.py --build-dir build/ci-recovery-release --fullscreen -j8 --display-jobs 2 --junit artifacts/ci-recovery/recovered-native.xml --artifacts artifacts/ci-recovery/recovered-native

Ubuntu24.04 x86_64/GCC13.3, release=1 (-O3 -s), existing pinned SDL3.4.16 and codecs. Each display fixture owns its declared Xvfb/Openbox screen; Mesa25.2.8 Lavapipe is explicit. Build command: GLOB2_SDL3_PREFIX=<existing-pinned-prefix> GLOB2_GLSLANG=<pinned-glslang15.1> scons --build=build/ci-recovery-release release=1 -j6 engine-tests unit-tests. Exact commands/flags and clean provenance are retained with the focused repair evidence.

The final change after that full pass only moves the GameSpeed fixture's clock read before timer creation. Clean final source de693a01b7a4e2b304a86cce2d6b28c44d3b2314 (tree identical to current merged master 17e249a27) passes the affected three GameSpeed cases, zero failures/skips. The exact six-line fixture-only diff, current binary hashes, clean build identity and tree equality are recorded. All runtime source, assets and other native cases are unchanged. An injected 300ms scheduling delay deterministically fails the old measurement and passes the corrected one; see pause-clock-order evidence.

Additional retained recovery evidence:
- Initial clean broad native pass: green-native-junit.xml (2968 cases), plus full platform/PG16/Node24, Python build/runner/music/docs, CLI and compatibility checks in the top-level evidence archive.
- Linux/macOS per-tick equivalence, save continuity (including old Castor format6), sim66 golden, Polder historical topology/stock comparison and independent same-source macOS rows are retained in the initial evidence.
- Windows foundation diagnostic https://github.com/Globulation2/glob2/actions/runs/38873263518 passes actual compilation and focused generator execution at fdffb149a18006674a23b0e7e486fe9b8071544c. This covers the macro/include repairs through #1202, not the later torus presentation change. Source/toolchain metadata and JUnit are retained.
- Old full run 38861165281's Clang coverage completes 1675/1676 engine cases; sole failure is the same software-Vulkan mapping-limit crash. Its exported coverage/merge reports have no separate error. Metadata was inspected by HTTP ranges (about1MiB downloaded from the 46GB retained failure artifact), without deleting or downloading the full raw-profile archive. #1202 raises the ephemeral display and coverage runners to the validated1048576-map budget; exact unchanged-binary hosted replay passes six cases.

Coverage limits: local actual renderer execution is Mesa OpenGL/software Vulkan. Windows display tests are intentionally skipped by the native registry; Windows headless/compile success does not establish D3D pixel validation. No local Metal/D3D hardware gameplay session is claimed. Full hosted success is a configured-matrix checkpoint, not proof of unrun hardware. Release/store signing and publication are outside routine development CI.

Full local and focused logs/JUnit are accessible here and in the focused folders. Raw final captures remain locally available; relevant GL/SDL map captures and original failure evidence are also retained in torus-world-admission and vma-near archives. Earlier desktop-constrained captures were archived and byte-compared before local removal; the archive receipt is retained locally.
