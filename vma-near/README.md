Verification for PR #1202

Final source: fdffb149a18006674a23b0e7e486fe9b8071544c. Base: 6a73f66ae01ac320fc7b3c96bfa569db1b029035. Native tests were built cleanly at 51edc205f; the final commit only adds the same CI mapping setting to coverage and edits documentation. Ubuntu 24.04 x86_64, GCC 13.3.0, pinned SDL 3.4.16 and existing codec dependencies, release=1 (-O3 -s), no fast-math changes. Complete compiler commands and source provenance are retained.

Windows failure: https://github.com/Globulation2/glob2/actions/runs/38865544922 fails at TeamStatsSample.cpp because Windows headers define `near` as empty. `object-comparison.json` records four independent translation-unit compilations: ordinary original, original with -Dnear= (expected failure), renamed ordinary, renamed with -Dnear=. All three successful objects are identical SHA256 81b5c1c3ddd25317d036617da99f29a55ac1efc24d7b12be619d33c753d081dc. No simulation behavior changes or revision bump.

Local commands/results:

- GLOB2_SDL3_PREFIX=<existing-pinned-prefix> GLOB2_GLSLANG=<pinned-glslang-15.1> scons --build=build/ci-recovery-release release=1 -j6 engine-tests unit-tests: passed, clean source at 51edc205f.
- python3 test/run_tests.py --build-dir build/ci-recovery-release --filter 'TeamStatsPipeline/*' --junit artifacts/ci-recovery/vma-near/tests.xml: 21 passed, zero failures/skips. Covers publication checksums, worker counts, save/load and smoothing boundaries.
- python3 -m unittest discover -s test/build_system -p 'test_ci_*.py': 93 passed.
- python3 tools/check_docs.py: 309 documents, zero errors.
- python3 test/check_sim_revision.py --base origin/master: passed.

Linux driver diagnosis: the unchanged GCC13 master binary crashes reliably on hosted Ubuntu 24.04 in large-map captures and overlay recovery. Source-symbolized Mesa 25.2.8 stacks show lvp_descriptor_set_create calling memset(set->map,0,64) with set->map=MAP_FAILED. /proc maps count is 262146 against vm.max_map_count=262144. Baseline: https://github.com/Globulation2/glob2/actions/runs/38870484027 . The exact hosted binary passes locally at the host's existing 1048576 limit, with measured peak 283312 maps. No user-host sysctl was changed.

A queue-depth reduction and a terrain-binding cache were measured locally and discarded because neither reduced mapping usage. Neither experiment is shipped. The repair changes only the ephemeral hosted runners' mapping budget; full test coverage remains enabled and no runtime dependency is added or upgraded. It does not establish product behavior on hosts with lower mapping limits or fix Mesa's unchecked allocation failure.

The authoritative hosted budget replay is https://github.com/Globulation2/glob2/actions/runs/38872991725 . It uses the exact unchanged failing master executable, source/flags/dependencies verified from the CI program archive, the same Mesa version, Vulkan validation enabled, and vm.max_map_count=1048576. It runs each affected case separately three times and samples /proc mappings. All six executions passed: large-map capture 123.98, 122.87 and 123.83 seconds; overlay recovery 15.51, 8.66 and 14.16 seconds. Peak 289938 mappings across 6010 samples, below 1048576 and above the original 262144 limit. Logs, original JUnit and captured images are retained in the archive.

Earlier budget diagnostic 38872741722 selected two cases in a registry job intended for one case and produced selection-accounting errors after normal process exits. It is not accepted as test evidence. Earlier Windows dispatch 38872868723 used an abbreviated SHA unsupported by actions/checkout and did not compile; corrected run https://github.com/Globulation2/glob2/actions/runs/38873263518 uses the full final SHA. Actual Windows success is not claimed before that run completes.

Coverage rationale: identifier-only production change has byte-identical generated code and all affected statistics tests. CI configuration changes are validated with exact failing binaries on hosted Linux. Prior full local suite and cross-platform simulation/save continuity evidence in this evidence branch remains applicable. The eventual full master matrix is the recovery checkpoint; focused diagnostics are not a full hosted checkpoint. No visual or gameplay feel changes are intended.
