# PR #601 validation evidence

This evidence branch is separate from maintained source documentation.

[Download the validation bundle](evidence.tar.gz) (31 MB compressed). It contains final test/build logs, public-fixture saves, continuation checksums, browser screenshots/traces, and explicitly labeled earlier size/performance measurements. Private save contents are excluded.

Final PR revision: `c5bcb5304`. Full native suite: 927 cases, zero failures/errors, 80 skips; final telemetry regression and 14 Chromium checks pass. See `artifacts/save-size/pr-review/README.md` inside the bundle for exact validation scope and revision boundaries. Hosted CI remains separate.

SHA-256: `74cc6ecdfb77edb9d826c008057d2d85f7be7e9ae9f20259ae25b3173a2241b4`.

Extract with `tar -xzf evidence.tar.gz`. The continuation reproduction script is `artifacts/save-size/pr-review/continuation/run.py`; run it from a built repository checkout.
