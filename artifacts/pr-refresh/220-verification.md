Local verification

- Tested commit SHA: b71e295c1555b4da2a176719ca0bc35d47e96cd8
- Base: 526f607f919ea57c9aa1735944848953c8941d69 (rebased PR head).
- Environment: macOS, arm64; Python 3.14.7; Apple Clang 21.0.0.
- Commands: `python3 tools/artwork/package_runtime.py --check; uv run --with numpy --with scipy --with pillow python tools/artwork/validate_runtime.py; python3 tools/artwork/runtime_provenance.py --check; GLOB2_SDL3_PREFIX=/Users/bradley/glob2/artifacts/recording-sdl3/prefix CCACHE=1 scons -j6 release=1 tests; python3 test/run_tests.py --filter RuntimePack/* --filter HighResolutionIntegration/* --artifacts artifacts/pr-refresh/220-render`
- Result: All 2,287 frames and source hashes validated; 91,136 terrain joins checked; candidate preparation/rejection passed; four software/OpenGL renderer cases passed. Selected current renderer captures attached.
- Limitations: macOS arm64 software/OpenGL only; external model inference, other platforms and human gameplay/performance review not performed. Original PR history preserved on archive branch.
