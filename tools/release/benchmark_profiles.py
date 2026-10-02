#!/usr/bin/env python3
"""Retain paired process-launch/simulation measurements and exact execution evidence.

Run on one host with two warmups and two batches of seven alternating pairs.
Compiler defaults are never changed here. A credible repeated slowdown, a
process-launch regression over 10%, or insufficient archive savings rejects adoption.
"""

import argparse
import hashlib
import json
import math
import os
import statistics
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))
FIXTURES = ROOT / "test/fixtures/javascript"


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def repeatable_slowdown(pairs):
    ratios = [candidate / baseline for _, baseline, candidate in pairs]
    medians = [
        statistics.median(
            candidate / baseline
            for batch, baseline, candidate in pairs
            if batch == value
        )
        for value in sorted({batch for batch, _, _ in pairs})
    ]
    slower = sum(ratio > 1 for ratio in ratios)
    # A one-sided sign test needs no distribution assumption or extra package.
    probability = sum(
        math.comb(len(ratios), n) for n in range(slower, len(ratios) + 1)
    ) / 2 ** len(ratios)
    return (
        len(medians) >= 2 and all(value > 1 for value in medians) and probability < 0.05
    )


def run(binary, fixture, output, correctness=False):
    output.mkdir(parents=True, exist_ok=False)
    (output / "profile").mkdir()
    command = [
        str(binary),
        "--run-game",
        "--load-game",
        str(fixture),
        "--ticks",
        "256",
        "--compute-threads",
        "1",
        "--output-dir",
        str(output),
    ]
    if correctness:
        command += ["--telemetry", "checksums", "--replay", "true", "--save", "final"]
    started = time.perf_counter()
    with (output / "engine.log").open("w") as log:
        subprocess.run(
            command,
            cwd=ROOT,
            stdout=log,
            stderr=subprocess.STDOUT,
            check=True,
            env=dict(os.environ, GLOB2_USER_DATA_DIR=str(output / "profile")),
        )
    elapsed = time.perf_counter() - started
    result = json.loads((output / "result.json").read_text())
    return dict(
        wall_seconds=elapsed,
        headless_setup_seconds=result["setup_ns"] / 1e9,
        simulation_seconds=result["run_ns"] / 1e9,
        ticks=result["ticks"],
        termination=result["termination"],
    )


def benchmark(baseline, candidate, output, baseline_size, candidate_size):
    output = Path(output).resolve()
    output.mkdir(parents=True, exist_ok=False)
    binaries = dict(
        baseline=Path(baseline).resolve(), candidate=Path(candidate).resolve()
    )
    evidence = dict(
        schema=1,
        revision=baseline_size["revision"],
        platform=baseline_size["platform"],
        architecture=baseline_size["architecture"],
        profile=candidate_size["profile"],
        binaries={
            name: dict(path=str(path), sha256=digest(path))
            for name, path in binaries.items()
        },
        scenarios={},
        process_launch=[],
    )
    for scenario in ("profile1", "realistic-profile1"):
        fixture = FIXTURES / (scenario + "-initial.game.gz")
        checks = {}
        for name, binary in binaries.items():
            directory = output / scenario / "correctness" / name
            run(binary, fixture, directory, correctness=True)
            checks[name] = {
                file: digest(directory / file)
                for file in ("game.replay.checksums", "game.replay", "final.game.gz")
            }
        if checks["baseline"] != checks["candidate"]:
            raise ValueError("Profile changes execution, replay or save: " + scenario)
        rows, pairs = [], []
        for batch in range(2):
            for repeat in range(-2, 7):
                order = (
                    ("baseline", "candidate")
                    if repeat % 2 == 0
                    else ("candidate", "baseline")
                )
                measured = {}
                for name in order:
                    row = run(
                        binaries[name],
                        fixture,
                        output / scenario / str(batch) / str(repeat) / name,
                    )
                    row.update(batch=batch, repeat=repeat, variant=name)
                    rows.append(row)
                    measured[name] = row
                if (
                    measured["baseline"]["ticks"] != measured["candidate"]["ticks"]
                    or measured["baseline"]["termination"]
                    != measured["candidate"]["termination"]
                ):
                    raise ValueError(
                        "Profile performs a different amount of work: " + scenario
                    )
                if repeat >= 0:
                    pairs.append(
                        (
                            batch,
                            measured["baseline"]["simulation_seconds"],
                            measured["candidate"]["simulation_seconds"],
                        )
                    )
        evidence["scenarios"][scenario] = dict(
            fixture_sha256=digest(fixture),
            checksums=checks,
            measurements=rows,
            repeated_slowdown=repeatable_slowdown(pairs),
        )
    launch_pairs = []
    for batch in range(2):
        for repeat in range(-2, 7):
            measured = {}
            order = (
                ("baseline", "candidate")
                if repeat % 2 == 0
                else ("candidate", "baseline")
            )
            for name in order:
                started = time.perf_counter()
                subprocess.run(
                    [str(binaries[name]), "--version"],
                    cwd=ROOT,
                    check=True,
                    stdout=subprocess.DEVNULL,
                    stderr=subprocess.DEVNULL,
                )
                measured[name] = time.perf_counter() - started
            evidence["process_launch"].append(
                dict(batch=batch, repeat=repeat, **measured)
            )
            if repeat >= 0:
                launch_pairs.append(
                    (batch, measured["baseline"], measured["candidate"])
                )
    process_launch_ratio = max(
        statistics.median(
            after / before for group, before, after in launch_pairs if group == batch
        )
        for batch in range(2)
    )
    # Compare a common archive format (the first archive in each inventory),
    # rather than confusing compiler savings with a switch from ZIP/gzip to xz.
    from tools.release.package_sizes import compare

    compare(baseline_size, candidate_size)
    before, after = (
        report["archives"][0]["bytes"] for report in (baseline_size, candidate_size)
    )
    saved = before - after
    adequate = saved >= 1024 * 1024 or (before and saved / before >= 0.01)
    evidence.update(
        archive_saved_bytes=saved,
        process_launch_ratio=process_launch_ratio,
        eligible_for_startup_and_renderer_review=bool(
            adequate
            and process_launch_ratio <= 1.10
            and not any(
                scenario["repeated_slowdown"]
                for scenario in evidence["scenarios"].values()
            )
        ),
        eligible_for_adoption=False,
        adoption_note="Actual application startup and renderer checks remain required; --version measures process launch only. Defaults remain unchanged.",
    )
    (output / "measurements.json").write_text(json.dumps(evidence, indent=2) + "\n")
    return evidence


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--baseline", type=Path, required=True)
    parser.add_argument("--candidate", type=Path, required=True)
    parser.add_argument("--baseline-size", type=Path, required=True)
    parser.add_argument("--candidate-size", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    benchmark(
        args.baseline,
        args.candidate,
        args.output,
        json.loads(args.baseline_size.read_text()),
        json.loads(args.candidate_size.read_text()),
    )


if __name__ == "__main__":
    main()
