# Final current-master integration verification

Tested commit: `eeb01dfdee857909a1ae5a535b14cfca11924aa4`. Base: `e77ecf5aa60de2862e502dfc838c0bfe335b1516`.
[Exact source tree and binary hashes](merge-source-manifest.json).
Host: Linux x86_64, GCC 13.3.0, Python 3.12.3. Fast development build uses -O0 -g1,
existing SDL/runtime dependency prefix; no new runtime dependency. This is not release-build validation.

Current master was merged into the reviewed feature. Only appended translation entries conflicted;
both the fourteen MCP labels and upstream hand-preference labels were preserved exactly once.
Independent UI review found no incompatibility with radial upgrades, fling changes or first-launch
hand preferences. This supersedes the earlier report's omission of newer-master integration.

- Desktop game and affected harness build PASS. [Build result](merge-build-result.log),
  [exact command](focused-build-command.json), [compiler commands](merge-compile-commands.json).
  The focused test helper's old link list was updated to include upstream HandPreferenceScreen;
  this was a tooling list correction, not a production-link failure.
- **79 integration cases PASS**, zero failures or skips: MCP protocol/HTTP/leases/input/capture,
  ScreenExecution, settings/capabilities/hand-choice persistence, GUI interaction/orders, all touch
  cases, buffered gameplay/editor receipts and retained chat text, portable renderers, resize,
  all ordered renderer transitions, recording and save safety.
  [Exact test command](merge-test-command.json), [log](merge-integration-tests.log),
  [JUnit](merge-integration-junit.xml). Timeout is 600 seconds; jobs=4, display-jobs=2.
- Live official MCP SDK 2.3.0 PASS for both 2026-07-28 and 2025-11-25: discovery, lease,
  chords, releases, inline PNG and stale viewport execution errors. [Results](merge-sdk-results.log).
  The socket was confirmed to belong to this task's isolated test game/profile.
- Golden match verification PASS: 842 checksum rows compare byte-for-byte with the committed trace.
  Command: `build/mcp-dev/src/glob2 match verify test/fixtures/multiplayer/FourSquares1.g2mr
  --map-file maps/FourSquares1.map.gz --output-dir artifacts/mcp/merge-golden-verify --compute-threads 2`.
  [Log](merge-golden-verify.log), [checksums](merge-golden-verify/checksums.txt),
  [verdict](merge-golden-verify/verdict.json). Simulation/save/replay compatibility gates were untouched.
- Dedicated relay rebuild PASS; [log](merge-relay-build.log). Previous symbol exclusion validation
  is in review-relay-exclusion.json; no MCP source or exclusion condition changed during integration.
- All fourteen affected browser source syntax checks PASS; [result](merge-browser-result.log),
  [command script](check-browser.py). Browser full link/runtime was not available.

The earlier independent review has [148 passing affected cases](review-reconciled-results.json),
including isolated reruns resolving two default-limit timeouts. One unchanged version-152 replay
expectation fails against the already-established minimum replay version 154;
[baseline contract](baseline-replay-contract.txt) and original logs retain that known failure.
The final integration selection explicitly targets changed boundaries and does not re-run that
unrelated contradictory expectation.

Coverage limits remain: no full Windows/macOS game runtime, mobile builds/runtime, native picker
interaction, device-loss injection, cross-platform checksum comparisons or release packaging.
Windows syntax-only checks and browser compile-only checks are explicitly documented in the
[earlier verification report](review-validation.md). Hosted PR checks were cheap contracts only.
Logs, images and review notes are published on the dedicated evidence branch, outside product docs.
