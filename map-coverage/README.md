# Clang coverage map fixture verification

Clean tested revision `7e337f20e95bcf9455c904158823bdbafa16b611`, base `b1a8603bedf079e587307c77b665e3090bd2d2e5`. Ubuntu 26.04 x86_64, Clang 18.1.8, GCC 15.2.0, SDL3 3.4.16 and pinned companion libraries. Retained provenance confirms both final binaries use the clean tested revision.

Hosted failure: https://github.com/Globulation2/glob2/actions/runs/37171997415/job/111347036189 at `69db1fb5db1c40e8037d571b8adbcbbfe7dd04ea`, Ubuntu 24.04 x86_64, Clang 18.1.3, unoptimized coverage. `hosted-engine-tests.log`: 565 engine groups passed; only this fixture failed with actual `15850274609968439542`, expected `5648058033288605271`. Local pre-repair coverage independently reproduces this exact mismatch (`coverage-current-before.log`).

`build-pre-random-coverage.py` substitutes the Fingerprint generation and option-construction body from `e674c61e3^` in the current coverage build, retaining current definition/registry metadata because the old controls lack the explicit search domains now required by the registry. The fixture forces historical pattern=0 and barrier=0; all other numeric defaults are unchanged. Only FingerprintGenerator.o is replaced in the identical engine link. `coverage-pre-random-commands.json` records exact argv arrays; substituted source is included.

Both historical-body runs reproduce exactly `15850274609968439542` (`coverage-old-before.log`, `coverage-old-repeat.log`). These intentionally fail against the incorrect pre-repair expected value. Their embedded TestMain provenance describes the surrounding current engine, not the substituted object. This compares historical generation bodies; it is not an entire historical-commit build. No numerical cause of build-dependent hashes is claimed.

The repair selects one exact historical hash for Linux x86_64, Clang major18, GLOB2_TEST_COVERAGE and absent __OPTIMIZE__. macOS and optimized Linux retain their existing exact hashes. No generator implementation, RNG, simulation revision, save format or replay/network gate changes.

Commands, from the product checkout; copy evidence files to artifacts/ci-repair:

```sh
GLOB2_SDL3_PREFIX=/home/bradley/glob2-verify/integrator/sdl3/prefix scons -j12 release=0 server=0 --build=build/native-coverage-repair engine-tests CC=clang-18 CXX=clang++-18 CFLAGS='-g -O0 -fprofile-instr-generate -fcoverage-mapping -DGLOB2_TEST_COVERAGE' CXXFLAGS='-g -O0 -fprofile-instr-generate -fcoverage-mapping -DGLOB2_TEST_COVERAGE' LINKFLAGS='-g -fprofile-instr-generate'
LD_LIBRARY_PATH=/home/bradley/glob2-verify/integrator/sdl3/prefix/lib LLVM_PROFILE_FILE=artifacts/ci-repair/coverage-final-%p.profraw GLOB2_TEST_ARTIFACTS_ROOT=artifacts/ci-repair/coverage-final python3 test/run_tests.py --build-dir build/native-coverage-repair --binary engine --filter 'MapGeneratorDefaults/Explicit designs preserve pre-Random golden worlds' --artifacts artifacts/ci-repair/coverage-final --junit artifacts/ci-repair/coverage-final.xml
GLOB2_SDL3_PREFIX=/home/bradley/glob2-verify/integrator/sdl3/prefix scons -j12 release=1 server=0 engine-tests
LD_LIBRARY_PATH=/home/bradley/glob2-verify/integrator/sdl3/prefix/lib GLOB2_TEST_ARTIFACTS_ROOT=artifacts/ci-repair/map-gcc-final python3 test/run_tests.py --binary engine --filter 'MapGeneratorDefaults/Explicit designs preserve pre-Random golden worlds' --artifacts artifacts/ci-repair/map-gcc-final --junit artifacts/ci-repair/map-gcc-final.xml
python3 artifacts/ci-repair/build-pre-random-coverage.py
LD_LIBRARY_PATH=/home/bradley/glob2-verify/integrator/sdl3/prefix/lib LLVM_PROFILE_FILE=artifacts/ci-repair/coverage-old-%p.profraw python3 test/run_tests.py --build-dir artifacts/ci-repair/coverage-pre-random --binary engine --filter 'MapGeneratorDefaults/Explicit designs preserve pre-Random golden worlds'
```

Final clean Clang coverage and optimized GCC each pass the complete five-design group; an earlier repaired Clang run also passed. Independent map-skill review found no blocking issue and requested avoiding an unproven floating-point explanation; corrected before final checks.

Focused coverage is appropriate to the test-only expected-hash selection. No local full coverage run, cross-platform determinism or human playtest is claimed. Master advanced to `5428cc3d295dc254067e48cc34944853304fb9fc` with themes, AI, telemetry and recording; affected generator/test sources remain unchanged. The next full current-master hosted matrix will provide integration and platform coverage for newer shared changes; this local evidence does not establish it green.
