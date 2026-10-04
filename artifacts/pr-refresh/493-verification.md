Local verification

- Tested commit SHA: b8ccbfb0716ec79c08a46a1856905daa9361aa84
- Base: a05d6cd8cbe594a4d281bb6e753edbe1b8d10899 (rebased PR head).
- Environment: macOS, arm64; Python 3.14.7; Apple Clang 21.0.0.
- Commands: `python3 -m unittest discover -s tools/unit-animation -p test_*.py`
- Result: 19 tests passed
- Limitations: No Blender render or art regeneration; engine unchanged.
