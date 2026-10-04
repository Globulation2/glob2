Local verification

- Tested final commit SHA: 13dc865444ea9fbeb41538d8f6d19d25d7136978
- Final base: 8bc1b897ff7b30b094d35dd86c3230828988c457 (rebased PR head).
- Environment: macOS, arm64; Python 3.14.7; Apple Clang 21.0.0.
- Native SDK where applicable: SDL3 3.4.16, SDL3_ttf 3.2.2, SDL3_image 3.4.6, SDL3_net 3.2.0; release=1, -j6, CCACHE=1.
- Final verification: Native binaries rebuilt on final rebased head; focused checks passed. Exact command filters and results in latest-master integration log; prior broader evidence remains linked at previous_validation_sha.
- Commands/results: `GLOB2_SDL3_PREFIX=<pinned SDK> CCACHE=1 scons -j6 release=1 tests; python3 test/run_tests.py --junit artifacts/pr-refresh/220-latest-master-junit.xml --filter 'HighResolutionIntegration/*' --artifacts artifacts/pr-refresh/220-latest-master-render; python3 test/check_sim_revision.py --base origin/master; git diff --check origin/master...HEAD`; final rebase and focused commands/results also recorded in 220-latest-master-integration.log where present.
- Limitations: macOS arm64 software/OpenGL only; external model inference, other platforms and human gameplay/performance review not performed. Original PR history preserved on archive branch.

Earlier broader validation

- Commit: b71e295c1555b4da2a176719ca0bc35d47e96cd8
- Base: 526f607f919ea57c9aa1735944848953c8941d69
- Commands: `python3 tools/artwork/package_runtime.py --check; uv run --with numpy --with scipy --with pillow python tools/artwork/validate_runtime.py; python3 tools/artwork/runtime_provenance.py --check; GLOB2_SDL3_PREFIX=/Users/bradley/glob2/artifacts/recording-sdl3/prefix CCACHE=1 scons -j6 release=1 tests; python3 test/run_tests.py --filter RuntimePack/* --filter HighResolutionIntegration/* --artifacts artifacts/pr-refresh/220-render`
- Results: All 2,287 frames and source hashes validated; 91,136 terrain joins checked; candidate preparation/rejection passed; four software/OpenGL renderer cases passed. Selected current renderer captures attached.

