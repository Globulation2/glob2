#!/usr/bin/env python3
"""Replay the Vultures bulk cases against one local Globulation 2 binary.

Input is the compact JSON case list retained in bulk-evidence.zip. Results keep the
full report, including validation details and generator telemetry, so a clean
geometry rejection cannot be mistaken for a crash or a candidate failure.
"""
import argparse
import concurrent.futures
import json
import os
from pathlib import Path
import subprocess
import tempfile
import time


def run_case(index, case, binary, timeout):
    params = case["params"]
    # The tournament framework stores dimension exponents, whereas the public
    # CLI takes actual tiles. All bulk dimensions are 64..512 (exponents 6..9).
    command = [binary, "--generate-map", "vultures", "--seed", str(case["seed"])]
    for axis in ("width", "height"):
        if axis in params:
            command += ["--" + axis, str(1 << params[axis])]
    for field in ("teams", "workers"):
        if field in params:
            command += ["--" + field, str(params[field])]
    for field in ("home-size", "lakes", "lake-size", "wheat-amount", "wood-amount"):
        if field in params:
            command += ["--set", field + "=" + str(params[field])]
    started = time.monotonic()
    with tempfile.TemporaryDirectory(prefix="vultures-bulk-") as directory:
        report = Path(directory) / "report.json"
        command += ["--json", str(report)]
        # A separate profile prevents concurrent jobs from sharing preferences or
        # other user data. The report is the requested output; maps are deliberately
        # not written during this large generation-only check.
        env = dict(os.environ, GLOB2_USER_DIR=str(Path(directory) / "profile"))
        try:
            process = subprocess.run(
                command, env=env, capture_output=True, text=True, timeout=timeout
            )
            data = json.loads(report.read_text()) if report.exists() else None
            if data is None:
                status = "missing_report"
            elif data["generation"]["outcome"]["success"]:
                status = "completed" if process.returncode == 0 else "exit_mismatch"
            else:
                status = data["generation"]["outcome"]["error"]
            diagnostic = (process.stdout + process.stderr)[-2000:]
            return {
                "index": index, "case": case, "status": status,
                "exit_code": process.returncode, "seconds": time.monotonic() - started,
                "diagnostic": diagnostic, "report": data,
            }
        except subprocess.TimeoutExpired as error:
            return {
                "index": index, "case": case, "status": "timeout",
                "seconds": time.monotonic() - started, "diagnostic": str(error),
            }
        except (OSError, ValueError, KeyError) as error:
            return {
                "index": index, "case": case, "status": "runner_error",
                "seconds": time.monotonic() - started, "diagnostic": repr(error),
            }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("binary")
    parser.add_argument("cases")
    parser.add_argument("output")
    parser.add_argument("--jobs", type=int, default=4)
    parser.add_argument("--timeout", type=int, default=180)
    args = parser.parse_args()
    cases = json.loads(Path(args.cases).read_text())
    # Concurrency is a runner option, not a generator parameter. Four processes
    # use less than half the 16-core devlaptop host and mirror its idle preflight.
    with concurrent.futures.ThreadPoolExecutor(max_workers=args.jobs) as workers:
        futures = [workers.submit(run_case, i, case, args.binary, args.timeout)
                   for i, case in enumerate(cases)]
        with Path(args.output).open("w") as output:
            for future in concurrent.futures.as_completed(futures):
                result = future.result()
                output.write(json.dumps(result, ensure_ascii=False) + "\n")
                output.flush()  # retain progress if a host or runner is interrupted
    print(f"wrote {len(cases)} case results to {args.output}")


if __name__ == "__main__":
    main()
