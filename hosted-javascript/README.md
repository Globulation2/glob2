# Hosted JavaScript failure classification

All three hosted failures are baseline test drift, unrelated to recording codecs.

Tested PR SHA: `9b2162f3bf346cd7f115ab25ba3c1d09d0ad51c7`. Compared baseline: `e67cba82e7ff1738efdfc65bf3a7c47dc8f7dcd1`.
The failing files have identical Git blob hashes in both revisions (table below).

| Toolchain | Unit corpus | Engine corpus | Original artifact |
| --- | --- | --- | --- |
| gcc11 | 25 cases passed | 63 cases, one case fails three literal checks | [Artifact 13060322347](https://github.com/Globulation2/glob2/actions/runs/41244011872/artifacts/13060322347) |
| clang | 25 cases passed | 63 cases, one case fails three literal checks | [Artifact 13059684162](https://github.com/Globulation2/glob2/actions/runs/41244011872/artifacts/13059684162) |
| windows | 25 cases passed | 63 cases, one case fails three literal checks | [Artifact 13060825411](https://github.com/Globulation2/glob2/actions/runs/41244011872/artifacts/13060825411) |

Every engine report fails only `Current simulation rejects earlier replays and enforces acceptance boundaries` in `src/scripting/javascript/ScriptCompatibilityTest.cpp`:

- Line 139 expects replay floor 186; actual is 187.
- Line 140 expects format version 186; actual is 187.
- Line 142 expects network protocol 72; actual is 73.

Both executable provenance checks pass in every artifact. No other JavaScript corpus case fails.

Baseline commit [`cbc20d2aab` / PR #1308](https://github.com/Globulation2/glob2/commit/cbc20d2aabc364f7727439659001d29ffc741c25) raised format/replay gates to 187 and network protocol to 73 while leaving these three hardcoded test expectations unchanged. The recording PR does not change these files. This is an upstream test expectation regression; no recording production change is indicated by these failures.

| File | Identical PR/base blob |
| --- | --- |
| `src/scripting/javascript/ScriptCompatibilityTest.cpp` | `0380c5e70fd9bf09c494544fd7bf68f07ca63336` |
| `src/app/Version.h` | `d1d608c3c93ccba9265d61bd53dae68c8489f747` |
| `src/replay/ReplayReader.h` | `9d5dc9995a1c642369ff4e15fc0e9bd3d3b09fea` |

`classification.json` contains failure details, source hashes, artifact IDs/URLs and corpus exit/provenance summaries. Each toolchain directory retains only unit/engine logs, JUnit reports and the small provenance manifest from the original artifact. Large unrelated source/profile evidence is omitted. No source files were edited during classification.
