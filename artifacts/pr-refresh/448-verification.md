Local verification

- Tested final commit SHA: a5c2ad0e8c9352ea9d4bf96707671cefc808265b
- Final base: 8bc1b897ff7b30b094d35dd86c3230828988c457 (rebased PR head).
- Environment: macOS, arm64; Python 3.14.7; Apple Clang 21.0.0.
- Native SDK where applicable: SDL3 3.4.16, SDL3_ttf 3.2.2, SDL3_image 3.4.6, SDL3_net 3.2.0; release=1, -j6, CCACHE=1.
- Final verification: Whitespace check passed; rebasing added only newer master changes, with no feature changes. Prior icon/mailmap/privacy checks remain applicable.
- Commands/results: `git diff --check origin/master...HEAD`; final rebase and focused commands/results also recorded in 448-latest-master-integration.log where present.
- Limitations: Documentation only; no APK or privacy publication.

Earlier broader validation

- Commit: 36e75abc33dd65e5aa2acc17c180fc76b73fbdd4
- Base: a05d6cd8cbe594a4d281bb6e753edbe1b8d10899
- Commands: `git diff --check origin/master...HEAD`
- Results: Resolved conflict against current online-service policy; whitespace check passed

