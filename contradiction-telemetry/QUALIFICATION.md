# Native qualification and instrumented restart

Game source `79de5484410beee884f85ba293bb44d38f226c55`; test-only stub repair `31eeb937cc3e9b0f591809ea8302b662b8cc911f`; frozen-deadline/pool tooling `f65c8d1698f6deefc4820209e69ad704f91d5776`. Ordinary GCC13.3 release, no PCH/unity. ELF SHA256 `abcbd2cf0de3891c7471432aef2788071bef3b3dbe17c3c0de073ff7b80981ae`. Model and production scheduling remain unchanged.

## Checks

- 15 engine cases and14 unit cases passed, including contradiction classification/RNG and gradient tests. Original hot calling interfaces and the compiled ordinary selector remain without tracking arguments/helper calls.
- Eight old/new × observer off/on × one/four-thread arms matched raw world/snapshot/full-replay vectors and final save bytes. Eight old-save continuations, exploration parity,15 delayed-publication traces and24 pending-phase continuations passed. Original save-cadence and RSS-censor failures, diagnoses and unchanged-assertion reruns are retained.
- All four remote hosts then ran the exact same2,048-tick scenario through the permanent coordinator. Their vectors, final saves and all emitted telemetry were byte-identical; original engine vectors and query labels matched. Permanent augmented export accepted the new columns and non-overlapping totals. See crosshost/verified.json and its archive.
- All55 Python suites passed for the later collection-tool changes. See collection-tools-pools/ for exact source hashes, tested source archive, logs and independent review.

## CPU limits

The longer River comparison (eight pairs,4,096 measured ticks) had a +0.40% paired median CPU/tick change with probes off, ranging −0.84% to +0.87%. The short large8v8 comparison was −0.49% with probes off and −1.49% with probes on. These are maps/window-specific preliminary measurements, not proof of zero overhead or a general speedup. Short initial River results are also preserved. Only our verified obsolete collection engines were temporarily paused, with exact-PID/starttime checks, guaranteed resume/watchdogs and pause receipts; unrelated jobs were not controlled. Host/governor/resource caveats are in the reports. The selector-only benchmark observed about26ns/query additional classification cost, not whole-game cost.

## Restart

The fresh128-game round uses16 preselected slots per eight formats, exact original map bytes/seeds, and the new contradiction contract. Static area×players workload routing sends80 large slots to the rig/dev laptop and48 general slots to all four hosts; all slots are retained. Identical ELF bundle aliases merge into one cohort/binary identity in export. The unchanged90,000-tick cap has a12-hour failure deadline to avoid retrying deterministic slow probes against a two-hour timeout. Localhost and the offline third Pharaoh host are excluded. The superseded old campaign retains67 successes and11 deliberately interrupted partial attempts, with50 never-started slots; it is diagnostic only and will not be relabelled for augmented fitting.

No new trained-model benefit is claimed. Windows/macOS/browser execution for this observer revision and manual gameplay are not verified. PR remains draft. The first refit must compare baseline and augmented variants on identical new rows, weights and grouped splits before considering any inference/bookkeeping changes.

Native archive reassembly and verification commands are in native/README.md. Recorded absolute campaign paths are provenance; archived relative files and their hashes provide the reviewable contents.
