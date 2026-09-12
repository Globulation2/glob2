#!/usr/bin/env python3
"""Run and verify Maxima's sealed-enemy dig-out scenario."""

from __future__ import annotations

import argparse
import re
import subprocess
from pathlib import Path

from make_maxima_digout_fixture import OUTPUT_MAP, SOURCE_MAP, build_fixture


SEED = 1296318279
FIELD = re.compile(r"(?:^|\t)([a-z_]+)=([^\t]+)")

# This profile only shortens the economic/military warm-up.  It does not force
# a target, create a clearing flag, or call the dig-out path directly.
FAST_READINESS_OVERRIDES = ",".join(
    (
        "military.endgame_force_floor=4",
        "military.campaign_force_floor=4",
        "military.endgame_population_floor=12",
        "military.campaign_population_floor=12",
        "military.campaign_population_base=12",
        "military.endgame_population_discount=0",
        "military.campaign_worker_floor=4",
        "military.campaign_worker_floor_base=4",
        "military.campaign_worker_population_ratio=10",
        "military.campaign_food_percent=100",
        "military.campaign_sustainable_food_percent=100",
        "military.defense_reserve_floor=0",
        "military.reserve_enemy_bonus=0",
        "military.reserve_force_divisor=20",
        "tactics.min_force=4",
        "postures.campaign_base=300",
        "military.first_barracks_population_min=4",
    )
)


def fields(line: str) -> dict[str, str]:
    return dict(FIELD.findall(line))


def tick(line: str) -> int:
    return int(line.split("\t", 2)[1])


def candidate_observer(line: str) -> bool:
    parts = line.split("\t", 4)
    return (
        len(parts) >= 4
        and parts[0] == "NICOWAR_OBSERVER_TELEMETRY"
        and parts[2] == "0"
        and parts[3] == "Maxima"
    )


def main() -> int:
    root = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--binary", type=Path, default=root / "build-tournament/src/glob2"
    )
    parser.add_argument("--map", type=Path, default=root / OUTPUT_MAP)
    parser.add_argument("--seed", type=int, default=SEED)
    parser.add_argument("--max-steps", type=int, default=12000)
    parser.add_argument(
        "--natural",
        action="store_true",
        help="use production readiness thresholds (usually needs many more steps)",
    )
    parser.add_argument(
        "--no-regenerate",
        action="store_true",
        help="run the existing fixture without rebuilding it from the source map",
    )
    args = parser.parse_args()

    if not args.binary.is_file():
        parser.error(f"binary not found: {args.binary}")
    if not args.no_regenerate:
        build_fixture(root / SOURCE_MAP, args.map)
    if not args.map.is_file():
        parser.error(f"map not found: {args.map}")

    command = [str(args.binary)]
    if not args.natural:
        command.extend(("--maxima-overrides", FAST_READINESS_OVERRIDES))
    try:
        map_argument = str(args.map.resolve().relative_to(root.resolve()))
    except ValueError:
        map_argument = str(args.map)
    command.extend(
        (
            "-nicowar-telemetry",
            "-nicowar-scenario-match-nox",
            map_argument,
            str(args.seed),
            "2",  # players
            "7",  # candidate: Maxima
            "5",  # opponent: original Nicowar
            "0",  # candidate seat
            "0",  # map position offset (Maxima is team 0, outside the wall)
            str(args.max_steps),
        )
    )
    result = subprocess.run(command, cwd=root, text=True, capture_output=True)
    lines = (result.stdout + result.stderr).splitlines()

    dig_lines = [line for line in lines if "\tdig_out_started\t" in line]
    clearing_lines = [
        line
        for line in lines
        if candidate_observer(line)
        and int(fields(line).get("clearing_flags", "0")) > 0
        and int(fields(line).get("clearing_flag_units", "0")) > 0
    ]
    siege_lines = [
        line
        for line in lines
        if "\tmission_selected\t" in line and fields(line).get("kind") == "siege"
    ]
    assault_lines = [
        line
        for line in lines
        if candidate_observer(line)
        and int(fields(line).get("war_flags", "0")) > 0
        and int(fields(line).get("warriors_attacking", "0")) > 0
    ]

    failures: list[str] = []
    if result.returncode:
        failures.append(f"engine exited with status {result.returncode}")
    if not dig_lines:
        failures.append("Maxima never emitted dig_out_started")
    if not clearing_lines:
        failures.append("no Maxima workers were observed on a clearing flag")
    if not siege_lines:
        failures.append("Maxima never selected a siege after opening the wall")
    if not assault_lines:
        failures.append("no warriors were observed attacking through a war flag")

    if dig_lines and clearing_lines and tick(clearing_lines[0]) < tick(dig_lines[0]):
        failures.append("clearing activity preceded the dig-out decision")
    if dig_lines and siege_lines and tick(siege_lines[0]) <= tick(dig_lines[0]):
        failures.append("siege was not selected after the dig-out decision")
    if siege_lines and assault_lines and tick(assault_lines[0]) <= tick(siege_lines[0]):
        failures.append("assault activity did not follow siege selection")

    if failures:
        for failure in failures:
            print(f"FAIL: {failure}")
        print("Relevant engine output:")
        relevant = [
            line
            for line in lines
            if not line.startswith("Maxima strategy:")
            and (
                "TELEMETRY" in line
                or "RESULT" in line
                or "error" in line.lower()
                or "assert" in line.lower()
            )
        ]
        for line in relevant[-30:]:
            print(line)
        return 1

    dig = fields(dig_lines[0])
    print(
        "PASS: sealed enemy detected at tick "
        f"{tick(dig_lines[0])}; {dig['flags']} clearing flags requested"
    )
    print(
        f"PASS: clearing workers active at tick {tick(clearing_lines[0])}; "
        f"siege selected at tick {tick(siege_lines[0])}"
    )
    print(f"PASS: warriors attacking through the breach at tick {tick(assault_lines[0])}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
