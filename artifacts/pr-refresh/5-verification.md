Local verification

- Tested commit SHA: 8f05ee98a2109a6b4e3a613a802f53577edb735c
- Base: a05d6cd8cbe594a4d281bb6e753edbe1b8d10899 (rebased PR head).
- Environment: macOS, arm64; Python 3.14.7; Apple Clang 21.0.0.
- Commands: `git check-mailmap nct/leo/marv/genixpro contacts; git diff --check origin/master...HEAD`
- Result: Four ambiguous legacy none@none authors resolve correctly; whitespace check passed
- Limitations: Author mappings preserved; draft remains draft.
