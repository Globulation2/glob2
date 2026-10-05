# AI Studio review evidence

Tested PR head: `722e4658615cb05d7f0e679c27875799177d8a25`.
Integrated base: `934c3c588bea51ec7421152e15f5168b2aaf5502`.

This dedicated evidence branch contains generated review artifacts, not application source. The archive includes the verification manifest with exact commands, build/test logs, native and browser checksum traces, saves/replays, and screenshots. `review-evidence-manifest.json` inside the archive records SHA-256 digests for every included file.

Three reviewers covered backend/billing, editor/state, and runtime/hosting. Two substantive review rounds and subsequent integration audits addressed provider recovery, capped reconciliation, checkout identity, stale edits, undo, diagnostics, platform boundaries and migration ordering.

The Linux x86_64 native build uses GCC 15.2.0 and pinned SDL3 dependencies. Browser verification covers Chromium, Firefox and WebKit, both serial and threaded WebAssembly. Platform tests use Node22.22.1, with Node24.19.0 for direct TypeScript execution in UI fixtures. The production-header smoke uses Caddy2.10 and the actual built game; private API/provider results are fixtures.

No live paid model calls or Stripe transactions were made. Native Windows/macOS coverage and a human maintainer playthrough remain outside this verification. The feature remains disabled by default until deployment configuration and that playthrough are complete.
