# PR 1197: primary hiring gradients

Tested production commit: `c7f1cd2996a13ac59d56926755c182008b67d736`. Integrated master base: `486a2230a4ddb257ba9dee34d3aace05f2260d6f`. PR remains draft.

## Production behavior

Hiring keeps the current obstacle-free ranking fallback for missing/stale routes. After ranking and coordination, its worker lazily prepares only primary ground candidates. The fallback suffix remains published and can use normal lazy extension. A new cold field publishes with the shortlist after one tick, guarded by building identity, slot epoch and topology. Existing serving fields are reused without moving refresh deadlines. Usable fields may already cover fallback cells through normal ranking or a shared expansion.

Format 171 persists private prepared fields awaiting publication; old hiring formats load without that record. Minimum save floor remains 58; replay floor is 171 and SIM_REVISION is 70. The multiplayer and affected market traces were regenerated. The scheduling-cap change remains separate in PR 1220.

## Verification

- 91 focused engine cases covered hiring/planning, cold gradient preparation, invalidation, scheduling, binary/text continuation, legacy format-170 loading, admission, market consumers and save safety. Ninety passed on the first final run; save safety encountered disk exhaustion and passed on its isolated retry. No focused failure remains.
- Four turn-engine checks passed, including the committed match-record trace. Nine protocol/client checks passed, including local simulation identity and unsupported-version reporting. Seven replay unit cases passed. `test/check_sim_revision.py --base origin/master` passed.
- Four headless games (two maps, seed 42, 2,048 ticks, one/four participants) have byte-identical per-tick world and entity traces within each map. Maps: Holiday Island 2 (128×128, four teams) and Oazis (256×256, eleven teams). Saves, replays, commands and environments are attached.
- macOS ARM64 / Apple LLVM 21, fast development build. These are correctness results, not performance measurements. Exact flags, binary hashes and JUnit reports are in qualification.json.

## Open limitations

The broader unit run executed 1,186 cases and had 22 failing cases: 20 display cases, 16-bit RGBA decoding, and OpenCL convergence. It is not a clean full-suite pass. No source changes under libgag/, src/render/ or src/field/ are present relative to master, but the failures have not been qualified against a baseline. See broader-unit-failures.json, macos-unit.xml and logs.tar.gz.

Linux compilation was interrupted by host disk exhaustion, after which Docker Desktop reported unable to start. Cross-platform equality is not established. Windows/browser/mobile and production release qualification are unverified, and maintainer play review remains outstanding. Only rebuildable object/archive files in this task’s benchmark build were removed to reclaim space; benchmark records and executables were preserved.

## Evidence contents

- qualification.json and JUnit: exact source, environment, coverage and limits.
- logs.tar.gz: build/test logs, including failed attempts and successful retries.
- saves-and-behavior-artifacts.tar.gz: save/continuation and behavior evidence.
- headless-saves-replays-traces.tar.gz: four native game runs, final saves, replays and checksums.
- broader-unit-artifacts.tar.gz: evidence retained from the wider unit run.
- implementation.patch: production commit for review.

The separate benchmark folder contains the previous 384-run optimized experiment. It used the four-per-team scheduling proposal in all variants; it is not a performance qualification of the current production commit.
