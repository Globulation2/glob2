Local verification

- Tested commit SHA: 612cf6b4ed464a759112de3b859dbf02bc736daa
- Base: 526f607f919ea57c9aa1735944848953c8941d69 (rebased PR head).
- Environment: macOS, arm64; Python 3.14.7; Apple Clang 21.0.0.
- Commands: `GLOB2_SDL3_PREFIX=/Users/bradley/glob2/artifacts/recording-sdl3/prefix CCACHE=1 scons -j6 release=1 tests; uv run --with numpy python -m unittest discover -s music -p 'test_*.py'; python3 test/run_tests.py --filter 'MusicSet/*' --filter 'SoundMixerTrackSelection/*' --filter 'Settings/*640x480 in OpenGL*' --filter 'TurnEngineHarness/the committed*' --artifacts artifacts/pr-refresh/214-render; python3 test/check_sim_revision.py --base origin/master`
- Result: Native test binaries build; five mixer/music/settings/golden cases pass; six Python checks pass and one repeat-synthesis case skips. Audio settings capture inspected and localization corrected.
- Limitations: macOS arm64 only; offline repeat synthesis skipped without pinned sound bank. No fresh full soundtrack render, other-platform checks or human listening review. Draft retained. Earlier failed build iterations superseded by final logs.
