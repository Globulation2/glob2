Local verification

- Tested final commit SHA: 56f9068e1808d48cd0174b36e0535cf826d7b3f4
- Final base: 8bc1b897ff7b30b094d35dd86c3230828988c457 (rebased PR head).
- Environment: macOS, arm64; Python 3.14.7; Apple Clang 21.0.0.
- Native SDK where applicable: SDL3 3.4.16, SDL3_ttf 3.2.2, SDL3_image 3.4.6, SDL3_net 3.2.0; release=1, -j6, CCACHE=1.
- Final verification: 19 unit-animation tests passed again on final rebased head; whitespace check passed.
- Commands/results: `python3 -m unittest discover -s tools/unit-animation -p test_*.py; git diff --check origin/master...HEAD`; final rebase and focused commands/results also recorded in 493-latest-master-integration.log where present.
- Limitations: No Blender render or art regeneration; engine unchanged.

Earlier broader validation

- Commit: b8ccbfb0716ec79c08a46a1856905daa9361aa84
- Base: a05d6cd8cbe594a4d281bb6e753edbe1b8d10899
- Commands: `python3 -m unittest discover -s tools/unit-animation -p test_*.py`
- Results: 19 tests passed

