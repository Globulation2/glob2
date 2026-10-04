Local verification

- Tested final commit SHA: c1794546c221dbe86bed3d43db99d30a5336b712
- Final base: 8bc1b897ff7b30b094d35dd86c3230828988c457 (rebased PR head).
- Environment: macOS, arm64; Python 3.14.7; Apple Clang 21.0.0.
- Native SDK where applicable: SDL3 3.4.16, SDL3_ttf 3.2.2, SDL3_image 3.4.6, SDL3_net 3.2.0; release=1, -j6, CCACHE=1.
- Final verification: Native binaries rebuilt on final rebased head; focused checks passed. Exact command filters and results in latest-master integration log; prior broader evidence remains linked at previous_validation_sha.
- Commands/results: `GLOB2_SDL3_PREFIX=<pinned SDK> CCACHE=1 scons -j6 release=1 tests; python3 test/run_tests.py --junit artifacts/pr-refresh/214-latest-master-junit.xml --filter 'MusicSet/*' --filter 'Settings/*' --filter 'SoundMixerTrackSelection/*' --filter 'TurnEngineHarness/the committed*' --artifacts artifacts/pr-refresh/214-latest-master-render; python3 test/check_sim_revision.py --base origin/master; git diff --check origin/master...HEAD`; final rebase and focused commands/results also recorded in 214-latest-master-integration.log where present.
- Limitations: macOS arm64 only; offline repeat synthesis skipped without pinned sound bank. No fresh full soundtrack render, other-platform checks or human listening review. Draft retained. Earlier failed build iterations superseded by final logs.

Earlier broader validation

- Commit: 612cf6b4ed464a759112de3b859dbf02bc736daa
- Base: 526f607f919ea57c9aa1735944848953c8941d69
- Commands: `GLOB2_SDL3_PREFIX=/Users/bradley/glob2/artifacts/recording-sdl3/prefix CCACHE=1 scons -j6 release=1 tests; uv run --with numpy python -m unittest discover -s music -p 'test_*.py'; python3 test/run_tests.py --filter 'MusicSet/*' --filter 'SoundMixerTrackSelection/*' --filter 'Settings/*640x480 in OpenGL*' --filter 'TurnEngineHarness/the committed*' --artifacts artifacts/pr-refresh/214-render; python3 test/check_sim_revision.py --base origin/master`
- Results: Native test binaries build; five mixer/music/settings/golden cases pass; six Python checks pass and one repeat-synthesis case skips. Audio settings capture inspected and localization corrected.

