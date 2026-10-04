Local verification

- Tested commit SHA: cc3ff3d0bab79172f70da86a696d463f30bbab8d
- Base: a05d6cd8cbe594a4d281bb6e753edbe1b8d10899 (rebased PR head).
- Environment: macOS, arm64; Python 3.14.7; Apple Clang 21.0.0.
- Commands: `python3 mobile/icons.py; git diff --exit-code; Pillow mode/background checks`
- Result: 13 opaque iOS icons reproduce exactly; Android unchanged; whitespace check passed
- Limitations: No signed archive, simulator gameplay or store publication.
