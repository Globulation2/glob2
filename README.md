# Snapshot checksum PR #1221 review evidence

Current tested source: [f47d5eba3](https://github.com/Globulation2/glob2/commit/f47d5eba32b7fa9b21ea8d29e4e6b4f21081e11e), based on `486a2230a4ddb257ba9dee34d3aace05f2260d6f`. Simulation revision 69; format/protocol 168/72; minimum save version 58.

- [Three-version Callgrind investigation and correction](checksum-callgrind-report.txt)
- [Download raw profiles, commands, copied drivers, 72 fresh native runs and controls](checksum-callgrind-review.tar.gz)
- [Profile evidence inventory and hashes](checksum-callgrind-inventory.json)
- [Earlier final assessment and complete performance table](final-review-report.txt)
- [Final source/binary hashes, environment, validation limits and decision](final-review-verification.json)
- [Download final review evidence](final-review-validation.tar.gz): exact commands, build logs, all 160 final release timing runs, JUnit results, deep/routine traces, saves, replays and continuation/base comparisons.
- [Integrity hashes](SHA256SUMS)

Coverage is computed from the recorded setup and frozen for the complete session. AI-only turn recordings and playback agree, and the last human quitting cannot change coverage. Replay integration tests read the terminal checksum.

**Updated assessment:** hashing remains cheaper than master and the review fixes do not increase its instruction count. The earlier CPU table did not establish a regression caused by those fixes. Keep the PR in draft while the separate coverage decision remains unresolved. Ten alternating matched pairs for four workloads and 1/4 participants establish no elapsed-time improvement, and all CPU-saving intervals include zero. Small/four-participant CPU cost increased 8.19% (approximate paired 95% interval: +0.70% to +16.22%). Latency results are mixed. No universal engine TPS/FPS improvement is claimed.

The final frozen-policy build passes 41 focused engine cases, 49 unit cases, 21 Python checks and two external replay controls through their terminators. The 41-case run passed without retries (300s per-case limit; hostile-order case 63.4s). Four 256-tick deep traces agree with the base and across 1/4 participants. Uninterrupted candidate saves/replays/routine traces are byte-identical across participant counts. Four continuations agree on deep gameplay traces; three legacy workloads retain a dormant enrollment-generation drift also present on the base, with exact differing bytes recorded. All 160 ordinary timing runs agree on outcome fields and have stable endpoint hashes within each algorithm.

Map sampling is not a network detection bound: with continuous advancement, 32-tick samples appear in default 25-turn reports every 800 turns, before delivery/network latency. Pauses change alignment. The guide now states this explicitly.

Windows/macOS/mobile, actual browser execution, rendered feel, packaging, remote-session performance, other hardware and longer workloads remain unverified. Timing uses ordinary full-feature release builds, without fast/PCH/unity options.

Intermediate evidence is retained separately: [604749af4 assessment](followup-report.txt), [its inventory](followup-verification.json), [its archive](followup-validation.tar.gz); [9449c5a82 inventory](verification.json), [older validation](final-validation.tar.gz), and [historical profiles](historical-performance.tar.gz). Earlier measurements are not pooled with the final frozen-policy measurements. The intermediate grouped stress-test timeout and its isolated pass remain in that earlier archive. Serializer field identification is also retained there; the serializer has not changed.

This dedicated evidence branch is excluded from the code PR.

The follow-up compares master `486a2230`, pre-fix `9449c5a`, and current `f47d5eb`. Strict interior serial windows show 30–49% fewer hashing instructions than master, with identical before/current hash counts. Whole-engine instructions decrease 0.19–0.58% versus master and differ by less than 0.03% before/current. Snapshot/executor/result handoff and terminal observations account for additional work outside the hash body; these predate the review fixes. Worker Callgrind counts are not native cycles.

A separate fresh 12-triplet small-workload timing cohort (72 processes) did not reproduce the earlier +8.19% current/master four-participant CPU result: it measured −0.88% [−8.13%, +6.95%]. Current/pre-fix was −11.79% [−18.06%, −5.03%] in this cohort; this does not isolate the cached getter as the cause of that timing difference. The older runs remain above as historical evidence, not proof that hashing or the fixes regressed. No universal throughput improvement is established. See the new report for reconstruction details, symbol-build controls, intervals and limits. No production source changed during this investigation.
