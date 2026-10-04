Local verification

- Tested final commit SHA: 65f84e211677fc732d2f7c6d459ae345dff00065
- Final base: 8bc1b897ff7b30b094d35dd86c3230828988c457 (rebased PR head).
- Environment: macOS, arm64; Python 3.14.7; Apple Clang 21.0.0.
- Native SDK where applicable: SDL3 3.4.16, SDL3_ttf 3.2.2, SDL3_image 3.4.6, SDL3_net 3.2.0; release=1, -j6, CCACHE=1.
- Final verification: Whitespace check passed; rebasing added only newer master changes, with no feature changes. Prior icon/mailmap/privacy checks remain applicable.
- Commands/results: `git diff --check origin/master...HEAD`; final rebase and focused commands/results also recorded in 430-latest-master-integration.log where present.
- Limitations: No signed archive, simulator gameplay or store publication.

Earlier broader validation

- Commit: cc3ff3d0bab79172f70da86a696d463f30bbab8d
- Base: a05d6cd8cbe594a4d281bb6e753edbe1b8d10899
- Commands: `python3 mobile/icons.py; git diff --exit-code; Pillow mode/background checks`
- Results: 13 opaque iOS icons reproduce exactly; Android unchanged; whitespace check passed

