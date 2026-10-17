# Snapshot checksum PR #1221 evidence

Current follow-up: [604749af4](https://github.com/Globulation2/glob2/commit/604749af4), based on `486a2230a4ddb257ba9dee34d3aace05f2260d6f`. Executable inputs match `84ca3efe3`; the final commit changes documentation only.

- [Follow-up assessment and performance table](followup-report.txt)
- [Source/binary hashes, environment, validation limits and coverage decision](followup-verification.json)
- [Downloadable follow-up evidence](followup-validation.tar.gz): build logs, exact commands, all 160 release timing runs, JUnit reports, traces, saves, replays, continuation checks and base reproductions.
- [Integrity hashes](SHA256SUMS)

The AI-only turn replay failure is fixed through a shared setup-based coverage policy. Replay integration tests now read the terminal checksum. Simulation revision is 68; format/protocol remain 168/72 and the save floor remains 58.

**Decision: keep the PR in draft. The measured benefits do not justify accepting the reduced routine checksum coverage for production.** Ten alternating matched pairs for four workloads and 1/4 participants show no consistent elapsed-time improvement. Scripted/four-participant CPU cost improves by 3.61% (approximate paired 95% interval: -5.91% to -1.24%); other CPU intervals include zero. Raw latency, memory and allocation data are retained. No universal engine TPS/FPS claim is made.

Validation covers 40 focused engine cases, 49 unit cases, 21 Python checks and one browser-reference replay through its terminator. A stress test timed out under contention and passed alone in 115 seconds; both reports are retained. Four 256-tick deep traces agree with the base and across participant counts; uninterrupted candidate saves/replays/routine traces are identical across 1/4 participants. Four continuations agree on deep traces. Three legacy workloads retain a dormant enrollment-generation difference also present on the base; exact payload comparisons and field identification are documented rather than silently normalized.

The guide now distinguishes map sampling from network detection. With continuous advancement, 32-tick map samples reach default 25-turn reports every 800 turns. Pauses change alignment; delivery and arbitration add latency.

Windows/macOS/mobile, actual browser execution, rendered gameplay/feel, release packaging, remote-session performance and other hardware/longer workloads remain unverified. Timing builds use ordinary full-feature release options, not fast/PCH/unity builds.

Earlier evidence is retained separately: [9449c5a82 inventory](verification.json), [earlier validation](final-validation.tar.gz), and [historical performance](historical-performance.tar.gz). Those archives predate these fixes and measurements.

This dedicated evidence branch is excluded from the code PR.
