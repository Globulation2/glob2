#!/usr/bin/env python3
"""Run deterministic headless scenarios for the Maxima farming policy."""

from __future__ import annotations

import argparse
import re
import subprocess
from pathlib import Path


SCENARIOS = (
    ("mixed_coastal_resources", "maps/Isles.map", 0x4D495845),
    ("damaged_barrier_regrowth", "maps/Migration.map", 0x44414D47),
    ("dense_midgame_wood", "maps/Garden_3.map", 0x574F4F44),
    ("narrow_islands", "maps/Holiday_Island_2.map", 0x49534C45),
    ("blocked_gates", "maps/Isles.map", 2718281828),
)

FIELD = re.compile(r"(?:^|\t)([a-z_]+)=([^\t]+)")


def fields(line: str) -> dict[str, str]:
    return dict(FIELD.findall(line))


def main() -> int:
    root = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--binary", type=Path, default=root / "build-tournament/src/glob2"
    )
    parser.add_argument("--max-steps", type=int, default=30000)
    parser.add_argument("--scenario", choices=[item[0] for item in SCENARIOS])
    args = parser.parse_args()
    selected = [item for item in SCENARIOS if not args.scenario or item[0] == args.scenario]
    failures: list[str] = []
    for name, map_file, seed in selected:
        command = [
            str(args.binary), "-nicowar-telemetry", "-nicowar-tournament-match-nox",
            map_file, str(seed), "0", str(args.max_steps),
        ]
        result = subprocess.run(command, cwd=root, text=True, capture_output=True)
        output = result.stdout + result.stderr
        policy = [line for line in output.splitlines() if "\tfarming_policy\t" in line]
        topology = [line for line in output.splitlines() if "\tfarming_barrier_topology\t" in line]
        cache = [line for line in output.splitlines() if "\tfarming_fertility_cache\t" in line]
        if result.returncode or not policy or not topology or len(cache) != 1:
            failures.append(f"{name}: incomplete run (exit={result.returncode})")
            continue
        maximum_policy_us = max(int(fields(line)["microseconds"]) for line in policy)
        if maximum_policy_us >= 5000:
            failures.append(f"{name}: policy refresh {maximum_policy_us}us >= 5000us")
        for line in topology:
            value = fields(line)
            if int(value["gates"]) != int(value["components"]) * 2:
                failures.append(f"{name}: component does not have exactly two gates")
                break
        started = sum(output.count(f"reason={reason}") for reason in (
            "boxed_in_gate", "mature_costly_gate", "wood_gate"))
        reopened = output.count("reason=gate_reopened")
        if name == "blocked_gates" and started and reopened < started - 1:
            failures.append(f"{name}: blocked gate clearing did not converge")
        print(
            f"{name}: passes={len(policy)} max_policy_us={maximum_policy_us} "
            f"barrier_components={max(int(fields(line)['components']) for line in topology)} "
            f"gate_clears={started}/{reopened}"
        )
    if failures:
        for failure in failures:
            print(f"FAIL: {failure}")
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
