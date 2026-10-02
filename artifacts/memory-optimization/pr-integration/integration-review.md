# PR 577 integration and independent review

Final source commit: 944302711e2c5e5edc4f36acc7a0002669cbcb4a. Client release and test builds passed on macOS arm64. Current-master execution: 420 unit cases, 46 focused persistence/continuation/untrusted-file cases and 7 replay/network/legacy compatibility cases passed (473 total). A focused allocator probe fails against the original block allocation and passes against the fixed code.

The 45,000-tick legacy fixture loads and saves with 2.02 GiB measured peak RSS; the resulting save reloads and advances to 45,002 ticks. Current master independently uses version127/protocol50, so its output is not byte-comparable with version126 outputs from the original benchmark. The original full detailed continuation and CPU acceptance evidence remains separately identified in peak-followup/review.md. These measurements have not been repeated as paired CPU benchmarks on the newer master.

Independent review found two allocation-failure ownership leaks, both fixed and rechecked. Added invariant comments explain chunked ownership, uninitialized tails, numeric bounds, original-header hashing, weak cache lifetime, exact canonicalization and autosave backpressure. Incoming master hardening was integrated with explicit256MiB compressed/2GiB expanded bounds, writer/reader acceptance parity and Maxima's existing16,777,216-entry collection bound. Exact-limit CRC and over-limit tests passed.

See independent-review.md for precise findings, resolution notes and optional cleanup: consistent formatting, a small shared SHA sequence helper, and RAII in pre-existing legacy gzip helpers. Linux/Windows execution, current-master server build and interactive playtest remain unverified locally; PR CI is pending. The prior validated source included passing client/server builds. Large current save payloads remain local; integration-validation.json contains their hashes.

## Final merge revision

Source5c4f47b01 is rebased onto master62b1988da and retains security fixes#571/#576. Final client/server release builds passed.474 cases passed (420 unit and54 engine persistence/security/continuation/compatibility). Final late legacy load/save peak was1.90GiB; saved bytes match the prior version127 integration output exactly. Independent reviewer found no new merge blockers. HostedCI was queued at merge preparation; no required status-check rule is configured on master. See finalize-validation.json for hashes, exact test counts and platform limits.
