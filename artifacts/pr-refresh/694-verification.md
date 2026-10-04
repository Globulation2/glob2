Local verification

- Tested final commit SHA: 5c5a84b3f099239e30c959a1c095f2ae082ba459
- Final base: 8bc1b897ff7b30b094d35dd86c3230828988c457 (rebased PR head).
- Environment: macOS, arm64; Python 3.14.7; Apple Clang 21.0.0.
- Native SDK where applicable: SDL3 3.4.16, SDL3_ttf 3.2.2, SDL3_image 3.4.6, SDL3_net 3.2.0; release=1, -j6, CCACHE=1.
- Final verification: Rebase succeeded without conflicts; build-system suite ran 297 tests, OK with one NSIS-compiler skip; YAML parsed and whitespace check passed.
- Commands/results: `python3 -m unittest discover -s test/build_system; uv run --with pyyaml python <yaml.safe_load workflow>; git diff --check origin/master...HEAD`; final rebase and focused commands/results also recorded in 694-latest-master-integration.log where present.
- Limitations: Workflow-only change; Windows/Epic packaging or upload not run from macOS. No release/tag changed.

