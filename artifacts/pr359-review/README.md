# PR #359 local verification

Tested revision: `2ee86366a6b722be3ca05a5fc7ea0061cc916fab`. Base: `0eecf07d33876deafdd78212596b3db4216c08f5`. PR head rebased on the base.

Environment: Linux x86_64, kernel 7.0.0-31-generic, glibc 2.43, Python 3.14.4.
A temporary virtual environment supplies NumPy 2.5.3 and SciPy 1.18.1 for the
existing optional statistical checks. The new duel solver requires only stdlib.
No native compilation or build flags are involved.

Commands (all exited 0):

```sh
/tmp/glob2-pr-review-venv/bin/python -m unittest discover -s test -p 'test_tournament*.py' -v
/tmp/glob2-pr-review-venv/bin/python -m unittest discover -s test -p 'test_duel_ratings.py' -v
/tmp/glob2-pr-review-venv/bin/python -m unittest discover -s test -p 'test_distributed_game_telemetry.py' -v
/tmp/glob2-pr-review-venv/bin/python -m unittest discover -s test -p 'test_fairness_model.py' -v
/tmp/glob2-pr-review-venv/bin/python test/test_map_fairness_tournament.py
/tmp/glob2-pr-review-venv/bin/python artifacts/pr359-review/validate_ratings.py
/tmp/glob2-pr-review-venv/bin/python -m compileall -q tools/tournaments tools/tournaments_ai_leaderboard.py
python3 -m tools.tournaments --help
python3 tools/tournaments_ai_leaderboard.py --help
git diff --check origin/master...HEAD
```

Results: 52 tournament/pipeline + 8 rating + 2 telemetry + 23 fairness-model
unit tests passed, with no skips. The five legacy fairness harness groups passed.
The synthetic 20,000-game / 10,000-pair / 60-generator cohort matched an independent
MM optimizer within 1.2e-9 Elo, ten shuffles gave exactly equal fits, and two
20-draw paired bootstrap runs with shuffled input gave identical results.
This is synthetic solver validation, not a new real-game tournament or performance
measurement. The script and complete test logs are retained alongside this file.

Coverage: scheduling and transfer lanes, immutable bundle checks, worker restart,
live configuration, packing and compact outcomes, explicit rules, parameter
sampling, balanced pairs, outcome policies, finite MLEs, seeds, duplicate records,
bootstrap coverage, stale daemon PID refusal, and existing statistical/telemetry
interfaces. New operational behavior changes what artifacts successful output-free
game jobs retain; failures and explicitly requested artifacts remain preserved.

Omissions: no live remote SSH fleet, macOS/Windows native processes, real-engine
tournament, timing benchmark, or native build was run. Engine source, AI rules,
simulation/save/replay/network code, dependencies and build inputs are unchanged;
no new simulation revision or cross-platform checksum claim is made. The reap
PID check and signal are not atomic. Existing historical calibration is not
recomputed. Hosted checks are separate from this local evidence.

Acceptance: the author accepts this focused local evidence for the offline-tooling
revision under the user's explicit instruction to review and merge.
