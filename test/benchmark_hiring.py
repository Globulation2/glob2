#!/usr/bin/env python3
"""Retain reproducible hiring diagnostics and reference-trajectory shadow evidence.

Workload JSON: [{"name": "colony", "save": "...game.gz", "ticks": 4096}].
Ticks are absolute. Timing and verification are deliberately separate invocations.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import subprocess
import time


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--baseline", type=Path)
    parser.add_argument("--workloads", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--mode", choices=["verify", "shadow", "timing"], required=True)
    parser.add_argument("--participants", type=int, nargs="+", default=[1])
    parser.add_argument("--repetitions", type=int, default=10)
    parser.add_argument("--verify-telemetry", nargs="*", choices=["hiring-stats", "hiring-shadow"],
                        default=["hiring-stats", "hiring-shadow"])
    parser.add_argument("--hiring-schedule", help="K:R:L:split|combined; enable same-planner owner/pool controls")
    args = parser.parse_args()
    if args.repetitions < 1 or any(n < 1 for n in args.participants):
        parser.error("positive repetitions and participants required")
    root = Path(__file__).resolve().parents[1]
    binary = args.binary.resolve()
    baseline = args.baseline.resolve() if args.baseline else binary
    workloads = json.loads(args.workloads.read_text())
    args.output.mkdir(parents=True, exist_ok=False)
    metadata = {
        "revision": subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=root, text=True).strip(),
        "diff_sha256": hashlib.sha256(subprocess.check_output(["git", "diff", "HEAD"], cwd=root)).hexdigest(),
        "host": platform.platform(), "machine": platform.machine(),
        "mode": args.mode, "binary": str(binary), "binary_sha256": digest(binary),
        "baseline": str(baseline), "baseline_sha256": digest(baseline),
        "workloads": [dict(w, save=str(Path(w["save"]).resolve()),
                           input_sha256=digest(Path(w["save"]))) for w in workloads],
        "limitations": ["shadow follows reference trajectory; no counterfactual economy or recruitment-delay proof",
                         "host activity is uncontrolled; inspect raw paired timing results"],
    }
    changed = subprocess.check_output(["git", "diff", "--name-only", "HEAD"], cwd=root, text=True).splitlines()
    changed += subprocess.check_output(["git", "ls-files", "--others", "--exclude-standard"], cwd=root, text=True).splitlines()
    metadata["changed_source_sha256"] = {name: digest(root / name) for name in sorted(set(changed)) if (root / name).is_file()}
    (args.output / "metadata.json").write_text(json.dumps(metadata, indent=2))
    variants = [("baseline", baseline, None), ("disabled", binary, None)]
    if args.mode == "verify":
        variants += [("stats" if telemetry == "hiring-stats" else "shadow", binary, telemetry)
                     for telemetry in args.verify_telemetry]
    elif args.mode == "shadow":
        variants = [("shadow", binary, "hiring-shadow")]
    if args.hiring_schedule:
        variants = [("disabled", binary, None), ("owner", binary, "hiring-shadow" if args.mode != "timing" else None),
                    ("pool", binary, "hiring-shadow" if args.mode != "timing" else None)]
    repetitions = args.repetitions if args.mode == "timing" else 1
    results = []
    with (args.output / "measurements.jsonl").open("w") as rows:
        for repetition in range(-1 if args.mode == "timing" else 0, repetitions):
            for workload in metadata["workloads"]:
                for participants in args.participants:
                    ordered = variants if repetition % 2 == 0 else list(reversed(variants))
                    for name, executable, telemetry in ordered:
                        directory = args.output / f'{workload["name"]}-{name}-n{participants}-r{repetition}'
                        directory.mkdir()
                        command = [str(executable), "game", "run", "--load-game", workload["save"],
                                   "--ticks", str(workload["ticks"]), "--compute-threads", str(participants),
                                   "--gpu", "off", "--output-dir", str(directory.resolve())]
                        if args.hiring_schedule and name in ["owner", "pool"]:
                            command += ["--hiring-schedule", args.hiring_schedule, "--hiring-execution", name]
                        if telemetry:
                            command += ["--telemetry", telemetry]
                        if args.mode == "verify":
                            command += ["--telemetry", "checksums", "--write-replay", "--save", "final"]
                        elif args.mode == "timing":
                            command += ["--benchmark-warmup", "256"]
                        started = time.monotonic()
                        with (directory / "engine.log").open("w") as log:
                            process = subprocess.Popen(command, cwd=root, stdout=log, stderr=subprocess.STDOUT)
                            if hasattr(os, "wait4"):
                                _, status, usage = os.wait4(process.pid, 0)
                                process.returncode = os.waitstatus_to_exitcode(status)
                                cpu = usage.ru_utime + usage.ru_stime
                            else:
                                process.wait(); cpu = None
                        row = dict(workload=workload["name"], variant=name, participants=participants,
                                   repetition=repetition, command=command, wall_seconds=time.monotonic()-started,
                                   cpu_seconds=cpu, exit_code=process.returncode)
                        if not process.returncode:
                            row["result"] = json.loads((directory / "result.json").read_text())
                            row["hashes"] = {str(p.relative_to(directory)): digest(p) for p in directory.rglob("*")
                                             if p.is_file() and (p.suffix in [".checksums", ".replay"] or p.name == "final.game.gz")}
                        rows.write(json.dumps(row) + "\n"); rows.flush(); results.append(row)
                        print(workload["name"], name, participants, repetition, process.returncode, flush=True)
                        if process.returncode:
                            raise RuntimeError(f"Run failed; inspect {directory / 'engine.log'}")
    if args.mode == "verify":
        for workload in workloads:
            for participants in args.participants:
                paired = [r for r in results if r["workload"] == workload["name"] and r["participants"] == participants]
                expected = paired[0]["hashes"]
                if not expected or any(r["hashes"] != expected for r in paired):
                    raise RuntimeError(f"Verification mismatch: {workload['name']} / {participants}")
        (args.output / "verification.json").write_text(json.dumps({"identical": True, "runs": len(results)}, indent=2))


if __name__ == "__main__":
    main()
