# Snapshot checksum PR evidence

Tested source: [9449c5a8213980b6c554b0dab080eae2c10171c4](https://github.com/Globulation2/glob2/commit/9449c5a8213980b6c554b0dab080eae2c10171c4). Integrated base: `486a2230a4ddb257ba9dee34d3aace05f2260d6f`.

- [Verification inventory, environment and source/binary hashes](verification.json)
- [Final validation archive](final-validation.tar.gz): exact commands, build/test logs, JUnit results, checksum traces, saves, replays and native browser-reference evidence. Profiles and executables are excluded.
- [Historical release performance archive](historical-performance.tar.gz): raw measurements and Callgrind profiles/annotations with source hashes and commands. These measurements predate the final one-tick delay and integration merge; they do not establish final-head throughput.
- [Archive integrity hashes](SHA256SUMS)

Final Linux validation passed: 229-case native suite; four JavaScript simulation cases; two replay playback checks; one preview/save-continuation check; 21 Python tests. Both JavaScript CLI profiles pass full traces, exact one/four-worker save/replay equivalence and six saved continuations. CPU work was confined to CPUs 9–15 and 25–31.

Windows, macOS, mobile, actual browser execution, rendered gameplay/feel, release packaging and final-head performance remain unverified. No cross-platform determinism or final-head speedup is claimed. Maintainer acceptance remains pending on the draft PR.

Artifacts are published on a dedicated evidence branch and are excluded from the code PR.
