# JavaScript generator sharing verification

Evidence for Globulation2/glob2 PR #964, feature head e0d929e0f2fd815c3a256fd7651a559edc30d24e, integrated with master e5bf091b968d298e2b0e07525a43cd4afce17acf.

- [Verification manifest and exact commands](verification.json)
- [Independent review and resolved findings](review.json)
- [Logs](logs/)
- [Native checks, saves, traces and build provenance](native-pr/)
- [Serial/threaded Chromium, Firefox and WebKit comparisons](determinism/)
- [Isolated canonical package, validation report, map and preview](isolated/)
- [Actual published release and room-generated map/setup](real-flow/)
- [Play/replay of that exact room map](real-play-pr/)
- [Website desktop/phone screenshots](screenshots/)

184 platform tests, 81 native cases, six browser determinism cases and two real worker/API flow cases passed. The API relay is a test fixture; gameplay/replay use real engines over a simulated turn network. Windows, macOS, Android and iOS native execution was not performed. See the manifest for dependency-cache workaround and coverage limits. Browser and native executed-binary provenance matches the clean final source. A PostgreSQL lock-table exhaustion during an over-concurrent attempt is retained alongside the successful bounded rerun.

These generated artifacts are deliberately on a separate evidence branch and are not part of the feature or master documentation tree.
