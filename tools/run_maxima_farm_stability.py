#!/usr/bin/env python3
"""Repeat live farming regressions; keep logs and fail on seed-reserve violations.

Use alongside MaximaFarmingIntegrationTest, which adversarially harvests and
regrows farms. Live games add real worker scheduling, opponents and construction.
A completed match is not proof of economic balance: this runner checks the
specific temporal seed invariant and records how many policy passes ran.
"""
import argparse
import concurrent.futures
import json
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]
MAPS = ("Holiday_Island_2", "Archipelago", "Isles", "Migration", "Garden_3",
        "A_big_pond", "Wild_River", "Sand_River")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, default=ROOT / "build/src/glob2")
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--steps", type=int, default=40000)
    parser.add_argument("--jobs", type=int, default=2)
    args = parser.parse_args()
    args.output_dir.mkdir(parents=True, exist_ok=True)

    def run(case):
        name, seed = case
        path = args.output_dir / f"{name}-{seed}.log"
        command = [str(args.binary.resolve()), "-nicowar-telemetry",
                   "-nicowar-scenario-match-nox", f"maps/{name}.map", str(seed),
                   "2", "7", "5", "0", "0", str(args.steps)]
        with path.open("w") as output:
            result = subprocess.run(command, cwd=ROOT, stdout=output,
                                    stderr=subprocess.STDOUT)
        lines = path.read_text(errors="replace").splitlines()
        # The runtime emits violations on every offending pass, even when its
        # ordinary periodic policy snapshot would otherwise be suppressed.
        violations = sum("\tfarming_seed_stability_violation\t" in line for line in lines)
        passes = sum("\tfarming_policy\t" in line for line in lines)
        completed = any(line.startswith("NICOWAR_SCENARIO_PLAYER_RESULT") for line in lines)
        return dict(map=name, seed=seed, exit=result.returncode, policy_samples=passes,
                    violation_events=violations, completed=completed,
                    passed=result.returncode == 0 and passes > 0 and completed and violations == 0)

    with concurrent.futures.ThreadPoolExecutor(max_workers=max(1, args.jobs)) as pool:
        results = list(pool.map(run, [(name, seed) for name in MAPS for seed in (42, 74241)]))
    (args.output_dir / "results.json").write_text(json.dumps(results, indent=2) + "\n")
    for result in results:
        print(json.dumps(result))
    return 0 if all(result["passed"] for result in results) else 1


if __name__ == "__main__":
    raise SystemExit(main())
